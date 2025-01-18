/*
 * This file contains all the metrics used in the program, including
 * k-anonymity, certainty score, and minimal_distortion.
 *
 * For purposes of speed, both scoring and k-anonymity are cached using
 * The score_cache and match_cache respectively. The extent to which these increase
 * performance, both under MinGen and GeneticAlgorithm, is so large that
 * it was entirely unexpected. What more, the hit rate (The amount of times
 * a value was pulled from the cache, rather than being calculated) sits
 * at an almost perfect 1.0. Even with the caches storing everything from rows
 * to entire tables (Implementation below) this is an exceedingly
 * impressive rate. You can actually see the cache work as the algorithms
 * "rev up," with the cache being populated, followed by a tremendous speedup
 * once they start getting hit. Originally, the speed of the title strobing
 * was tied to the amount of iterations, which made this effect more obvious
 * but MinGen rips through generations so quickly that the effect was a little
 * to uncomfortable to look at, so it now uses a static timing.
 *
 * I explicitly made a --no-cache flag to see just how pronounced the cache
 * is. Try it!
 *
 * The MinGen Reference, available here:
 * https://dataprivacylab.org/dataprivacy/projects/kanonymity/kanonymity2.pdf
 * Specifies a unique metric based on precision, which is just minimal_distortion -1:
 * "Precision of a generalized table is then one minus the sum of
 *  all cell distortions (normalized by the total number of cells)"
 * This solution elected to just use minimal_distortion and certainty_score as
 * they will be most familiar to the reader, with the former being practically
 * identical to that outlined in the reference.
 */

#pragma once

#include "table.h"

#include <chrono>   // For time in statistics.


namespace metric {

  // The form of metric to use.
  typedef enum metric {md, c} metric;

  // The score cache is used in the scoring functions. It is a tree
  // that walks through rows, returning the score of that particular
  // combination.
  static shared::Tree<float> score_cache = {};

  // The match_cache is used for k-anonymity. It is a tree that
  // walks through rows, returning the indexes of rows that match
  // that particular modification
  static shared::Tree<std::vector<size_t>> match_cache = {};

  // Premature pruning for statistics. Score trimming only lead
  // to a dozen or so trims from 100,000s of requests, so its not worth it.
  static size_t match_trims = 0;


  /**
   * @brief Calculate the minimal distortion.
   * @param w: The working table with the modifications.
   * @param o: The original table.
   * @param best: The current best, for premature terminations.
   * @returns A score
   */
  static float minimal_distortion(const table::Table& w, const table::Table& o, const float& best) {
    float score = 0.0;

    // Walk row by row
    for (auto iter = w.row_begin(); iter != w.row_end(); ++iter) {
      const auto& row = iter.get();

      // If we've run into this row before, just return the cached value
      if (shared::cache && score_cache.in(row)) score += score_cache.get(row);

      // Otherwise, increment the row score for each modification.
      else {
        float row_score = 0.0;
        for (size_t col = 0; col < w.columns(); ++col) {
          const auto& original = o.index(iter.current(), col);
          const auto& mod = row[col];

          // Since we just add one, just add the weight.
          if (original != mod) row_score += w.get_column(col).weight;
        }

        // Update the accumulator, update the cache.
        score += row_score;
        if (shared::cache) score_cache.add(row, row_score);
      }
    }
    return score;
  }


  /**
   * @brief Calculate the certainty score.
   * @param w: The working table with modifications.
   * @param o: The original table.
   * @param best: The current best, for premature terminations.
   * @returns The score.
   */
  static float certainty_score(const table::Table& w, const table::Table& o, const float& best) {
    float score = 0.0;

    // Iterate row-by-row
    for (auto iter = w.row_begin(); iter != w.row_end(); ++iter) {
      const auto& row = iter.get();

      // If cached, return it.
      if (shared::cache && score_cache.in(row)) score += score_cache.get(row);

      // Otherwise, generate the value
      else {
        float row_score = 0.0;
        for (size_t col = 0; col < w.columns(); ++col) {

          const auto& original = o.index(iter.current(), col);
          const auto& mod = row[col];
          float col_score = 0.0;

          // If its the same, no penalty; if it's been entirely suppressed, +1
          if (original == mod) continue;
          else if (mod == "*") ++col_score;

          else {

            // If there's a domain graph, and our value is in it, determine
            // The loss of information by using the more generalized
            // name
            const auto& column = w.get_column(col);
            const auto& graph = column.graph;
            if (!graph.empty() && !graph.find(mod).empty()) {
              col_score += float(graph.breadth(mod)) / float(column.unique.size());
            }

            // If its an integer column and we have a range, determine
            // the new bounds and compare the the original.
            else if (column.t == table::type::integer && mod[0] == '[') {
              col_score += float(shared::Range(std::string(mod)).range()) / float(column.range.range());
            }

            // This should never happen unless the arguments to the program are invalid,
            // the table is corrupt or invalid, or something wrong happened during execution
            else throw std::runtime_error("Invalid modification!");
          }
          col_score *= o.get_column(col).weight;
          row_score += col_score;
        }

        // Update score and cache.
        score += row_score;
        if (shared::cache) score_cache.add(row, row_score);
      }
    }
    return score;
  }


  /**
   * @brief Determine how many rows match a given obscured row.
   * @param o: The original table
   * @param row: The generalized row from the working table
   * @param c: The columns we're interested in . Everything up to this value INCLUSIVE is checked.
   * @returns The amount of matches in the Table.
   */
  static std::vector<size_t> match_row(const table::Table& o, const std::vector<std::string_view>& row, const size_t& c) {

    // If we already have this row cached, return it.
    // Notice we make no assumption about the original table, which means
    // If we matched against another table it would be completely wrong.
    if (shared::cache && match_cache.in(row, c)) return match_cache.get(row, c);

    // Our matches
    std::vector<size_t> matches = {};

    // Iterate through each row in the source.
    for (auto iter = o.row_begin(); iter != o.row_end(); ++iter) {
      const auto& compare = iter.get();
      bool match = true;

      // Iterate through each field in the row.
      for (size_t x = 0; x <= c; ++x) {

        // If the values directly match, or the field has been suppressed, it matches.
        if (row[x] == compare[x] || row[x] == "*") continue;

        // Check if the values can be defined as a generalization from the column's graph.
        // IE: Higher Education would match if the specialized value PhD.
        const auto& column = o.get_column(x);

        // Ignore non-quasi items.
        if (column.sensitivity != table::quasi) continue;

        // If there is a domain, find out if the value is contained within it.
        const auto& graph = column.graph;
        if (!graph.empty()) {
          auto path = graph.find(row[x]);
          if (std::count(path.begin(), path.end(), compare[x])) continue;
          path = graph.find(compare[x]);
          if (std::count(path.begin(), path.end(), row[x])) continue;
        }

        // If the column is integers, and we have a range, see if the value fits in it.
        else if (column.t == table::type::integer && row[x][0] == '[') {
          const auto range = shared::Range(std::string(row[x]));
          if (range.in(std::atoi(compare[x].data()))) continue;
        }

        // If none applied, this value is incongrugent, and doesn't match. Break.
        match = false;
        break;
      }

      // If we have a match, add it.
      if (match) matches.emplace_back(iter.current());
    }

    // Cache and return so we don't have to do this again.
    if (shared::cache) match_cache.add(row, matches, c);
    return matches;
  }


  /**
   * @brief Evaluate a set of matches to determine k score.
   * @param matches Each of the rows' matches rows indexes from the original.
   * @param tree: A ephemeral tree used for generation
   * @param ks: A list of the used values for each index.
   * @param x: The current row.
   * @info Determining the k-anonymity of a table is more difficult than you might
   * think; the intuitive solution is simply check each modified row, see how many
   * of the original rows it matches, and if that value is less than our k value
   * then return false. However, this method fails to take into account more nuanced
   * deductions about the data. Consider a table with two men, and two women, to which
   * one of the men has been suppressed entirely, while the other three have their genders
   * unchanged (I use this example because this was a bug that this solution addresses).
   * In this situation, if we used the naive approach it would technically work:
   * The unsuppressed male could match to any of the rows: k = 4
   * Either female could match to themselves or the other: k = 2
   * The unsuppressed male could match to themselves or the other male: k = 2.
   * What this fails to account is that because we know the genders of all people in the
   * set, its trivial to determine the suppressed male through elimination.
   * The solution? Take every possible match and create a solution tree where we check for
   * combinations that lead to valid solutions (A person cannot be assigned more than one
   * row in the modified set). We then collect these valid solutions, and find the unique
   * states for each row within THAT, which gives us a true k score.In the above example,
   * such a tree revealed that suppressed man and one of the women were always assigned to
   * a single row, which would only be a k of 1.
   */
  static void k_tree(const std::vector<std::vector<size_t>>& matches, std::list<size_t>& tree, std::vector<std::set<size_t>>& ks, const size_t& x = 0) {

    // If we're done recurisve search, update the ks by our
    // path down the tree, represented one valid way to
    // organize our matches.
    if (x == matches.size()) {
      size_t i = 0;
      for (const auto& x : tree) ks[i++].emplace(x);
      return;
    }

    // Otherwise, go through each match, see if our choices are still valid
    // Given the choices of the matches, and emplace those options if it is.
    for (const auto& option : matches[x]) {
      if (std::find(tree.begin(), tree.end(), option) == tree.end()) {
        tree.emplace_back(option);
        k_tree(matches, tree, ks, x+1);
        tree.pop_back();
      }
    }
  }


  /**
   * @brief Determine the lowest k-anonymity for columns 0-c
   * @param w: The working table.
   * @param o: The original table.
   * @param k: The k used in the metric.
   * @param c: The upper bound of columns to check; when we are
   * exhaustively searching, we can prune branches in the search space
   * because if a row can be uniquely identified using only a subset of the
   * columns in the mutated state, then it is guaranteed that the row
   * can be uniquely identified in the final transformation. Since we apply
   * our search column by column, we can check the state of our current column,
   * and all previous columns, and discard the search immediately if mutation
   * will yield a table that leads to a k score higher than what we want.
   * @returns Whether this table meets k-anonymity.
   */
  static bool k_anonymity(const table::Table& w, const table::Table& o, const size_t& k, size_t c = SIZE_MAX) {

    if (c == SIZE_MAX) c = w.columns() -1;

    // Get all the matches for each of the modified row.
    std::vector<std::vector<size_t>> matches;
    for (auto iter = w.row_begin(); iter != w.row_end(); ++iter) {
      const auto& row = iter.get();
      auto match = match_row(o, row, c);

      // Bail early if we find a row cannot even be matched here.
      if (match.size() < k) {++match_trims; return false;}

      matches.emplace_back(match);
    }

    // Use our tree algorithm to find the k score given our matches.
    auto tree = std::vector<std::set<size_t>>(matches.size());
    std::list<size_t> working = {};
    k_tree(matches, working, tree, 0);

    // Check to ensure each row can be matched
    // to at least k rows in all solutions.
    for (const auto& solutions : tree) {
      if (solutions.size() < k) return false;
    }
    return true;
  }

  // A modified k_anonymity for GeneticAlgorithm which returns the average k anonymity
  // of the rows, rather than a uninformative yes/no boolean.
  static float av_k_anonymity(const table::Table& w, const table::Table& o, size_t c = SIZE_MAX) {

    if (c == SIZE_MAX) c = w.columns() -1;
    float score = 0.0;

    // Get all the matches for each of the modified row.
    std::vector<std::vector<size_t>> matches;
    for (auto iter = w.row_begin(); iter != w.row_end(); ++iter) {
      matches.emplace_back(match_row(o, iter.get(), c));
    }

    // Use our tree algorithm to find the k score given our matches.
    auto tree = std::vector<std::set<size_t>>(matches.size());
    std::list<size_t> working = {};
    k_tree(matches, working, tree, 0);

    // Check to ensure each row can be matched
    // to at least k rows in all solutions.
    for (const auto& solutions : tree) {
      score += solutions.size();
    }
    return score / float(tree.size());
  }


  /**
   * @brief Helper function to print statistics
   * @tparam T: I couldn't be bothered to figure out the actual type for a C++ duration.
   * @param tables: The list of best tables
   * @param max: The max amount of iterations we ran.
   * @param total: The total amount of possible states we could've hit.
   * @param states: The amount of states we actually hit.
   * @param best: The best score.
   * @param duration: How long the operation took.
   * @info A note on implementation. A more prudent implementation to this would have been to create
   * an abstract Anonymizer class, to which MinGen and GeneticAlgorithm inherited from, to which
   * this would be a shared member, and to which the anonymize() could be defined from an abstract
   * function (You'll notice there's some pretty big code deduplication between those two functions).
   * I elected against that, however, because I didn't want to needless complicate the project, given
   * that we've already implemented two different types of trees and have some pretty dense concepts
   * like pruning, on top of trying to explain two different algorithms. Throwing an base class into
   * the mix, which would be the better decision for code library, would hinder the primary aims as
   * a learning aid.
   */
  template <typename T> void print_stats(
    const std::multiset<table::Table>& tables,
    const size_t& max,
    const size_t& total,
    const size_t& states,
    const float& best,
    const T& duration
  ) {

    // Print all the best
    shared::clear();
    std::cout << "===RESULTS===" << std::endl;
    for (auto x : tables) {
      x.update_widths();
      x.print();
      std::cout << std::endl;
    }

    // Some fun statistics.
    std::cout << (max == SIZE_MAX ? "Pruning reduced searched nodes to " : "Nodes reached before cutoff: ") << states;
    if (max == SIZE_MAX) std::cout << ", reducing total search by a factor of " << total/states;
    std::cout << std::endl;

    std::cout << "Execution time of: " << std::chrono::duration<float, std::milli>(duration) << std::endl;
    auto seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    if (max == SIZE_MAX) std::cout << "Speed: " << seconds/float(total) << "ns per state (Real: " << seconds/float(states) << " ns)" << std::endl;
    else std::cout << "Speed: " << seconds/float(states) << "ns per state" << std::endl;

    std::cout
      << "Hits to the K-Anonymity Cache: " << match_cache.total_hits() << " with rate of "
      << match_cache.hit_rate() << " (" << match_trims << " Trims)" << std::endl;
    std::cout
      << "Hits to the Score Cache: " << score_cache.total_hits() << " with rate of "
      << score_cache.hit_rate() << std::endl;

    std::cout << "Instances with best score of " << best << ": " << tables.size() << std::endl;
  }
}
