/*
 * This file contains a heavily optimized MinGen implementation.
 * The Reference for MinGen:
 * https://dataprivacylab.org/dataprivacy/projects/kanonymity/kanonymity2.pdf
 * outlines the idea behind the algorithm, but you almost certainly do not need to read
 * it. It's far more interested in the mathematics of k-anonymity, precision, and MinGen
 * then the actual implementation, but Figure 5 outlines the algorithm. These
 * are the steps:
 *    1. If the table already meets the k-anonymity requirement, return it.
 *    2. If not, generate every single possible permutation of the table.
 *    3. Return the one that meets the k-anonymity requirement with the lowest score.
 * The take away is that this just a brute-force algorithm, trying every single possible
 * combination and returning the best one; as the paper admits: this isn't a very
 * efficient process.
 *
 * Rather than simply implementing the algorithm directly, I elected to make an optimized
 * version of it. This problem, where we enumerate multiple possible states, and use
 * a scoring function to determine its fitness, is quite similar to algorithms for games
 * like Chess and Go, where we have an unfathomable search-space of all possible combinations
 * of a game board, and must determine the best branch to go down. In light of this, we can
 * use a modified version of Alpha-Beta pruning to dramatically accelerate search speed,
 * and unlike Chess or Go, can actually search the entire space in a reasonable time for
 * small tables. Alpha-Beta relies on two players, and keeps track of a best score and worst
 * score in the current stage of the search space, to which it can prune branches that will
 * never be taken if both players act perfectly. Likewise, we have a score that we keep
 * track of, and can make our own pruning, although it's more an Alpha pruning since there's
 * nobody else here.
 *
 * After each column, we check both the k-anonymity and score of the current column, and all
 * modified columns previously, against the original column. We check to see: if we cut the
 * table off here, what would the k-anonymity value be? If this value is greater than our
 * threshold, that means (assuming k=2) that we can uniquely identify one of the modified
 * rows using only the information within the modified columns (Like if all names are unique,
 * and we didn't suppress the name, then we can uniquely identify each row solely off the name).
 * If we can uniquely identify a row using only a subset of the modifications, then no additional
 * modifications will be able to change this, and we can safely prune this branch in the search space.
 * Likewise, both our scoring metrics strictly INCREASE score, meaning that the best you can score is
 * 0 if nothing changed. If the score of the subset is GREATER than our current best value, then
 * it is impossible for any further modification to lead to it being better (Or even equal) due to the
 * nature of the scoring function, and we can again prune it.
 *
 * These optimizations combine with heavy caching with both scoring functions, and can exhaustively cover
 * a search space of billions in a couple of seconds. Look at metrics.h for more details about that.
 */

#pragma once

#include <chrono>   // For time keeping.
#include <future>   // For async.

#include "shared.h"
#include "table.h"
#include "metrics.h"


/**
 * @brief The MinGen namespace
 */
namespace mg {

  // Our MinGen class.
  class MinGen {
    private:

    // The working copy that is changed, and the original that remains constant.
    // There are two different way we could go about going through each possible state of the Table:
    // 1. Recursively call a function on new table instances with our modifications.
    // 2. Keep a single working copy, and make changes, then remove them.
    //
    // The former option is cleaner to implement (But not by much), but suffers from catastrophic performance
    // Due to how many copies are made. With our tables containing a lot of auxiliary information, we can't afford
    // to make that many copies, so just keep a single copy, and modify it iteratively.
    table::Table working;
    const table::Table original;

    // K score and metric
    size_t k = 2;
    metric::metric m = metric::metric::md;

    // An upper bounds on iterations, current states reached
    size_t max = SIZE_MAX, states = 0;

    // A list of tables that share our high-score.
    std::multiset<table::Table> tables = {};

    // So the worker function can give the updater some periodic updates.
    table::Table view;
    std::mutex lock;

    // The current best table's score
    float best = INFINITY;

    // Reset
    void reset() {
      max = SIZE_MAX;
      states = 0;
      tables = {};
      best = INFINITY;
    }

    // View helpers.
    void update_view(const table::Table& update) {
      std::lock_guard<std::mutex> guard(lock);
      view = update;
    }
    table::Table get_view() {
      std::lock_guard<std::mutex> guard(lock);
      auto copy = view;
      return copy;
    }


    // Score the results
    void score_results() {
      // Increase the states
      ++states;

      // Get the score, replace it if necessary
      const auto score = m == metric::md ? metric::minimal_distortion(working, original, best) : metric::certainty_score(working, original, best);
      if (best == INFINITY || score < best) {
        best = score;
        tables = {};
        update_view(working);
      }

      // Add it to the list if it's good.
      if (score == best) tables.emplace(working);
    };


    /**
     * @brief The worker function.
     * @param row The current row
     * @param col: The current column
     * @info This function calls itself recursively, iterating through every row and column.
     */
    void anonymize_worker(const size_t& row, const size_t& col) {

      // If we've run out of iterations, return what we've got
      if (states == max) return;

      // If we've run out of columns, score.
      if (col == working.columns()) {score_results(); return;}

      auto& column = working.index(col);

      // Ignore non-quasi values
      if (column.sensitivity != table::quasi) {anonymize_worker(row, col + 1); return;}

      auto& cell = column.data[row];
      const auto copy = cell;

      // Get all the permutations of the cell
      for (const auto& mut : working.mutations(cell, column, (max != -1))) {

        // Return if we exhausted our states.
        if (++states >= max) return;

        // Make the change, update width so the tables prints nice.
        cell = mut;

        // We've reached the last column, move to the next
        if (row == column.data.size() - 1) {

          /*
          * If the modification does not anonymize the rows, or
          * we have already made more modifications than our best,
          * we can prune.
          *
          * This line also illustrates how minute changes in a code base
          * can have tremendous impact on performance, and how refined optimization can be:
          * Without pruning, an example dataset took 2s to calculate. However, the scoring function
          * is FASTER than k_anonymity, which means we can rely on the lazy-evaluation of boolean statements.
          * If the centrality score does not evaluate, then C++ will not even bother k_anonymity because anything
          * X && false = false Putting centrality first lead to a runtime of 0.2; putting k_anonymity first lead to
          * a runtime of 0.4. In computation time, 2 tenths of a second is a massive improvement, all from the
          * ordering of conditions in an if statement.
          */
          if ((m == metric::c ? metric::certainty_score(working, original, best) : metric::minimal_distortion(working, original, best) <= best) && metric::k_anonymity(working, original, k, col)) {
            if (col == working.header.size() - 1) score_results();

            // Move to the next column
            else anonymize_worker(0, col + 1);
          }
        }

        // Move to the next row
        else anonymize_worker(row + 1, col);

        // Once we've run through all the possible iterates with this mutation, revert the mutation.
        cell = copy;
      }
    }

    public:

    // Constructor to take the table to anonymize
    MinGen(const table::Table& o) : original(o) {reset();}

    /**
     * @brief Anonymize the table
     * @param k_val: The k value to use
     * @param sstr: The name of the column that has the sensitive value.
     * @param m_val: The metric to use
     * @param iters: The max amount of states to enter.
     */
    void anonymize(const size_t& k_val, const metric::metric& m_val, const size_t& iters) {

      // Update
      k = k_val;
      m = m_val;
      max = iters;
      working = original;


      // Check if we already meet k-anonymity
      if (metric::k_anonymity(working, original, k, original.columns() - 1)) {
        std::cout << "Already meets K-Anonymity Threshold!" << std::endl;
        return;
      }

      // Print the metric, and iterations.
      log("Scoring Metric: " << m);
      auto total = working.get_distinct();
      if (total == SIZE_MAX) {log("There are more states than your computer is capable of storing and displaying. Good luck.");}
      else log("Possible Configurations: " << total);
      if (max != -1) log("Non-Exhastive Search: Results may not be best");

      // Pick a mode, start the timer.
      shared::pick_mode();
      auto start = std::chrono::steady_clock::now();

      // PGO hates multi-thread, so we keep it within one thread when running for that.
      if (shared::single_thread) anonymize_worker(0, 0);


      else {

        // Start the worker.
        std::future<void> future = std::async(std::launch::async, &MinGen::anonymize_worker, this, 0, 0);

        table::Table sample = original;

        // For a smooth 60 FPS ;)
        bool old_best = INFINITY;
        size_t old_states = 0, speed = 0;
        for (size_t x = 0; future.wait_for(std::chrono::milliseconds(17)) != std::future_status::ready; ++x) {
          shared::clear();

          // Print the title
          std::cout << shared::print_title(shared::mg, x/5) << std::endl;

          // Update our current best when it changes.
          if (old_best != best) {
            sample = get_view();
            sample.update_widths();
            old_best = best;
          }

          // Print some statistics.
          std::cout << "States: " << states << "/" << total << " = ~" << speed << "/sec" << std::endl;
          std::cout << "Note: The above upper bound does not account for pruning!" << std::endl;
          std::cout <<  "Score: (Smaller is better): " << best << std::endl;

          // Update roughly twice per second.
          if (x % 30 == 0) {
            speed = (states - old_states)*2;
            old_states = states;
          }

          sample.print();
        }
      }

      // Get the duration, print the results, and reset.
      auto end = std::chrono::steady_clock::now();
      auto duration = end - start;
      metric::print_stats(tables, max, total, states, best, duration);
      reset();
    }
  };
}
