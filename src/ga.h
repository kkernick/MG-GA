/*
 * This file contains the Genetic Algorithm for finding anonymized tables.
 * Like MinGen (IE Brute-Force), a Genetic Algorithm is not so much like Wireguard,
 * where the algorithm has an explicit set of steps and methods, but more a general
 * approach that is customized on a case-by-case basis. The source that I consulted was:
 * https://books.google.com/books?id=GlGpDgAAQBAJ&dq=genetic+algorithm+evolutionary+algorithms&pg=PP2
 * but also like MinGen, you don't need to read it to understand the contents of this file.
 * Generally, a Genetic Algorithm consists of a few key, and broad, steps:
 *    1. Create an initial population
 *    2. Sort this population based on a fitness metric.
 *    3. Choose the best of the population, and generate a new population from these top results.
 *    4. Continue step 2-4 until an end condition is met, such as a certain score, or generation cap.
 *
 * For the MinGen implementation, the process was rather straightforward: the brute-force algorithm was
 * implemented, and then optimizations were added on top to speed up the calculations. Perhaps fittingly,
 * the development of the Genetic Algorithm took a more gradual, iterative approach, as I tried different
 * strategies.
 *
 * For this problem in particular, there are two key values that we want to express in a fitness score:
 *    1. The K-Anonymity
 *    2. The Scoring Function (IE Minimal-Distortion or Certainty)
 * With the latter, adding this into a fitness function is quite easy. Lower scores are better, so simply
 * use it as a denominator (We need to be weary of tables that don't make any changes, causing a /0)
 * K-Anonymity, however, is tricky: higher K-Score is better, but only to a point, and lower K-Score is
 * absolutely unacceptable for the final table. If we simply used K-Score as a numerator, the algorithm would
 * start valuing higher K, despite the user not needing more than the value they provided, and we'd get completely suppressed
 * tables to maximize K. For this, we can simply add a cap such that K values higher than the requested give no additional
 * fitness than getting the score exactly. However, the second issue is quite difficult to deal with: we should never
 * return a table that doesn't meet the requested K-Anonymity, but how can we enforce such a requirement in a formula?
 *
 * The first approach was simply giving K-Anonymity a high weight, and unsurprisingly,
 * the results were poor; the algorithm would never actually return a K-Anonymous table, with averages hovering around 1-1.75
 * (Target = 2). The problem was that in the search-space, K-Anonymous tables are exceedingly rare, and not even excessive
 * mutation rates (Which is essentially just stochastic) could find them.
 *
 * The next approach was simply removing K-Anonymity from the equation entirely: pull the initial population as a set of random,
 * K-Anonymous tables (We could use our MinGen implementation to simply iterate until it finds such a table, regardless of score),
 * and enforce mutations/inheritance such that the children are also K-Anonymous. Unfortunately, while this did produce results,
 * it was terribly slow. Since we were tying our initial generation to MinGen (Even if not a exhaustive search), we weren't getting
 * much faster than just using MinGen, which was the chief reason of this implementation. Enforcing K-Anonymity at each change also
 * prevented the algorithm from gradual evolution; say table S has a fitness of 15, and S' has a value of 20 (Higher is better for fitness)
 * but to reach S' we would need to make a series of changes to which the intermediaries would not be K-Anonymous. The only way for
 * this implementation to do this would be if it was exceedingly lucky and mutated all the changes in one pass, which constrained
 * evolution.
 *
 * Finally, and the implementation that exists below, was to simply run the algorithm in two passes (Originally, it was quite literally
 * two simulations with separate scoring functions, but has since been optimized into just a two-stage scoring function). The first
 * step is simply reaching K-Anonymity. If the Table isn't K-Anonymous, its score is simply the average K-Anonymity for all rows. It's only
 * once it's reached K-Anonymity that the S-Score function is added on top of that. This implementation works precisely as expected:
 * we can feed entirely random tables as the initial generation, do not need to enforce any requirements on changes and mutations, which
 * lets mutations run wild, and get very good (Although not necesssarily the best) results. The benchmark for this algorithm was table2.csv
 * in `examples`, which contains more states than C++ is capable of printing (Hence far out of reach for MinGen). This implementation, coupled
 * with optimizations via caching, can produce convincing tables for this input.
 *
 */

#pragma once

#include <chrono>   // For time measurement and statistics
#include <future>   // For async

#include "shared.h"
#include "table.h"
#include "metrics.h"


/**
 * @brief The Genetic Algorithm namespace
 */
namespace ga {


  // The instance is just a table and its fitness.
  // We want to store our Tables in a std::set, because then we don't actually
  // need to sort our Tables, we can let the Standard Library do it for us as we emplace.
  typedef struct instance {
    float s = 0.0f;
    table::Table table = {};
  } instance;

  // This is the function the set uses to compare instances. We want descending order (IE highest values first).
  static auto score_lambda = [](const instance& a, const instance& b) {return a.s > b.s;};

  // The Genetic Algorithm Implementation.
  class GeneticAlgorithm {
    private:

    // The original table, for score
    const table::Table original;

    // Bonuses, and the current best. K is a float because we return the average K across each rows.
    float k = 2.0, best = INFINITY;

    // The metric to use.
    metric::metric m = metric::md;

    // The generation limit, the size of each generation, states encountered, the amount of tables deemed
    // the best, the rate of mutations, amount of cells in the table, and current iteration.
    size_t max = 1000, population = 100, states = 0, cutoff = 10, m_rate = 10, cells = 0, iter = 0;

    // Random number generation, one for deciding who a table should partner with, another for mutations.
    // Mutation uses a simple scheme, where we roll a number from 0-100, and add our mutation rate on top
    // of that. Any value <= 50 we use the value from the first, any value > 50 <= 100 we use the value from
    // the second, and any value > 100 is a mutation. A mutation is just choosing at random one of the
    // possible ways a cell can be changed given Domains/Ranges; it's the same method that MinGen used to
    // iterate over all possibilities, it just shuffles them and we draw the top (The randomness was used
    // by the non-exhaustive MinGen so it didn't just return the same values over and over again).
    //
    // With a set of discrete cells, this mimics genetic recombination to a tee; the
    // result is that about half the cells are inherited from one parent,
    // the other half from the other, with a bit of mutations sprinkled in.
    std::uniform_int_distribution<> roll, mutations = std::uniform_int_distribution<>(0, 100 + m_rate);

    // Lets the main thread display what the worker is working on.
    instance view;
    std::mutex lock;

    // Using a multiset here makes things a lot easier for us, since it already manages sorting.
    // We can simply initialize a set, and populate it with the new generation and it will be
    // automatically ordered, letting us pop from the front for whatever percentile we're looking for.
    // A multiset is necessary because we want non-unique values.
    std::multiset<instance, decltype(score_lambda)> generation = {};


    /**
     * @brief Reset the state.
     */
    void reset() {
      best = INFINITY;
      states = 0;
      iter = 0;
    }


    // Thread-Safe control of the view.
    void update_view(const instance& update) {
      std::lock_guard<std::mutex> guard(lock);
      view = update;
      best = view.s;
    }
    instance get_view() {
      std::lock_guard<std::mutex> guard(lock);
      auto copy = view;
      return copy;
    }


    /**
     * @brief The fitness metric.
     * @param f: The table to check.
     * @returns The fitness
     * @info See the massive header comment.
     */
    inline float fitness(const table::Table& f) const {

      // If our table is K-Anonymous, then add our s_score to the final score.
      if (metric::k_anonymity(f, original, k)) {

        // The fitness takes our k_value; not the average, so a score greater than k just adds k to the score and prevents
        // the algorithm from liking excessive k-values when the user doesn't need it.
        float score = k;

        // S_Score will be, at most, 1, which means that simply dividing as-is would actually lead to this lowering fitness. To fix,
        // we just multiply it by a fixed value, in this case just the number of cells. There's no mathematical reason for this other
        // than giving K-Anonymous scores a boost.
        auto s_score = (m == metric::md ? metric::minimal_distortion(f, original, INFINITY) : metric::certainty_score(f, original, INFINITY));
        return ((score * cells) / s_score);
      }

      // Otherwise, the score is the average K-Anonymity for each row.
      else return  metric::av_k_anonymity(f, original) / k;
    }


    /**
     * @brief Combine two tables.
     * @param first: The first parent, modified in-place.
     * @param second: The second parent.
     * @info This function simulates a genetic recombination between the two tables, such that
     * the result is 50% of the resulting cells being from one parent, and 50% the other (Excluding
     * mutations).
     */
    void combine(table::Table& first, const table::Table& second) {
      for (size_t col = 0; col < original.columns(); ++col) {
        auto& fc = first.get_column(col); const auto& sc = second.get_column(col);

        // Don't try and mutate non-quasi values.
        if (fc.sensitivity != table::quasi) continue;

        for (size_t row = 0; row < fc.data.size(); ++row) {

          // Roll for what to do. The range is 0-100 + mutation
          // If we roll above 100, its a mutation
          // Otherwise < 50 borrow, >= 50 keep.
          auto mutate = mutations(shared::gen);

          // If we roll a mutation, get all possible values, and just choose one at random (The true in the mutations() call).
          if (mutate > 100) fc.data[row] = original.mutations(original.index(col).data[row], original.get_column(col), true)[0];

          // If it's less than 50, we take from the other table, otherwise we "take" from the first by not doing anything,
          // since we're modifying it in place.
          else if (mutate < 50) fc.data[row] = sc.data[row];
        }
      }
    }


    /**
     * @brief Run a simulation
     * @return The final, unique best.
     */
    std::multiset<table::Table> anonymize_worker() {

      // Lambda to get the best of the generation, emplace into a multiset, and return it.
      auto generate_return = [](const std::multiset<instance, decltype(score_lambda)>& gen) {
        std::multiset<table::Table> ret;
        float best = gen.begin()->s;
        for (const auto& solution : gen) {
          if (solution.s != best) break;
          ret.emplace(solution.table);
        }
        return ret;
      };


      // We want to gradually increase mutation rate.
      size_t tenth = max/10;

      // Iterate for max generations.
      for (iter = 0; iter < max; ++iter) {

        if ((iter + 1) % tenth == 0) {
          // This is probably one of the best features of this algorithm, and I don't know entirely
          // how standard it is. Basically, it gradually increases the mutation rate over time,
          // such that the Genetic Algorithm, to which beneficial traits are passed down by previous generations,
          // devolves into a largely stochastic algorithm (With the default m_rate of 10, the last 10% will
          // have a mutation rate of 3840:100). This ONLY works because we keep the best of the previous
          // generation, which means crazy mutations that tank score will just be left behind for the next
          // generation, but it allows us to break out of local-maximums by giving tables the ability to
          // pretty much re-roll every cell to something random.
          m_rate *= 2;
          std::uniform_int_distribution<> roll, mutations = std::uniform_int_distribution<>(0, 100 + m_rate);

          update_view(*generation.begin());
        }

        best = generation.begin()->s;

        // The next generation.
        std::multiset<instance, decltype(score_lambda)> children = {};

        // Cutoff determimes the top scores to keep, but we need to preserve the same generation size, so this determines
        // how many children each of the best need to produce, on top of including itself.
        size_t offspring = (population - cutoff)/cutoff;

        // Get the top score.
        for (size_t x = 0; x < cutoff; ++x) {

          // Get our current, put them into the next generation.
          auto current = *std::next(generation.begin(), x);
          children.emplace(current);
          ++states;

          // Make offspring.
          for (size_t y = 0; y < offspring; ++y) {
            ++states;

            // Get a partner from the top n; this can be itself.
            const auto& partner = *std::next(generation.begin(), roll(shared::gen));

            // Make a child
            combine(current.table, partner.table);
            current.s = fitness(current.table);
            children.emplace(current);
          }
        }

        // Replace
        generation = children;
      }

      // Once we're done if iterations, return the best.
      return generate_return(generation);
    }

    public:

    /**
     * @brief Construct a Genetic Algorithm instance.
     * @param o: The table to anonymize.
     */
    GeneticAlgorithm(const table::Table& o) : original(o) {
      cells = original.columns() * original.get_column(0).data.size();
    }


    /**
     * @brief Anonymize the table.
     * @param k_val: The K to use for K-Anonymity.
     * @param m_val: The metric to use for scoring.
     * @param iters: The amount of generations to create.
     * @param p_val: The size of each generation
     * @param mut_rate: The base mutation rate.
     */
    void anonymize(
      const size_t& k_val,
      const metric::metric& m_val,
      const size_t& iters,
      const size_t& p_val,
      const size_t& mut_rate
    ) {

      // Setup our values.
      k = k_val;
      m = m_val;
      if (iters != SIZE_MAX) max = iters;
      population = p_val;
      m_rate = mut_rate;

      // Make random tables.
      log("Generating Random Tables");
      for (size_t x = 0; x < population; ++x) {
        auto i = original.random();
        auto s = fitness(i);
        generation.emplace(instance{.s=s, .table=i});
      };
      roll = std::uniform_int_distribution<>(0, cutoff);

      // Some helpful info.
      log("Scoring Metric: " << m);
      auto total = original.get_distinct();
      if (total == SIZE_MAX) {log("There are more states than your computer is capable of storing and displaying. Good luck.");}
      else log("Possible Configurations: " << total);
      if (max != -1) log("Non-Exhastive Search: Results may not be best");

      shared::pick_mode();

      // Anonymize.
      auto start = std::chrono::steady_clock::now();

      std::multiset<table::Table> tables;
      if (shared::single_thread) tables = anonymize_worker();
      else {

        // Since when has C++ had async? To make it better, the GCC/Clang version uses a Thread Pool, which means this
        // costs us nothing to spin up, and with nice status functions to wait until its finished. This is why C++ is the best.
        std::future<std::multiset<table::Table>> future = std::async(std::launch::async, &GeneticAlgorithm::anonymize_worker, this);

        update_view(*generation.begin());
        auto sample = get_view();
        sample.table.update_widths();

        // For a smooth 60 FPS ;)
        for (size_t x = 0; future.wait_for(std::chrono::milliseconds(17)) != std::future_status::ready; ++x) {
          shared::clear();

          std::cout << shared::print_title(shared::ga, x/5) << std::endl;

          if (sample.s != best) {
            sample = get_view();
            sample.table.update_widths();
          }

          // Print some statistics.
          std::cout << "Generation: " << iter << "/" << max << std::endl;
          std::cout <<  "Fitness (Larger is better): " << sample.s << std::endl;

          // Print the current best table
          //sample->table.update_widths();
          sample.table.print();
        }
        tables = future.get();
      }

      auto end = std::chrono::steady_clock::now();
      auto duration = end - start;

      const auto& b = *tables.begin();
      best = (m == metric::md ? metric::minimal_distortion(b, original, INFINITY) : metric::certainty_score(b, original, INFINITY));
      if (!metric::k_anonymity(b, original, k)) {
        std::cout << "WARNING: Result is not k-anonymous! Increase iterations or population size!" << std::endl;
        getchar();
      }

      // Print some stats, and reset for another call.
      metric::print_stats(tables, max, total, states, best, duration);
      reset();
    }
  };
}
