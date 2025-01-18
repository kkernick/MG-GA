/*
 * This file contains shared functionality and classes.
 */

#pragma once

#include <vector>     // For vectors
#include <random>     // For randomness
#include <cassert>    // For assertions
#include <sstream>    // For string streams
#include <algorithm>  // For finding
#include <iostream>   // For printing

/**
 * @brief The shared namespace
 */
namespace shared {

  // Global values.
  static std::default_random_engine rng = {};
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static bool verbose = false, cache = true, single_thread = false;


  /**
  * @brief Clear the screen.
  * @remarks This works on UNIX/Windows. It's a shell escape sequence.
  */
  inline void clear() {std::cout << "\033[2J\033[1;1H";}


  /**
   * @brief Split a string based on a delimiter.
   * @tparam T: The type of value to return. Defaults to a vector.
   * @param in: The string to split.
   * @param delim: The delimiter to split on.
   * @returns: A vector containing the split elements.
   */
  template <typename T = std::vector<std::string>> T static split(const std::string& in, const std::string& delim) {
    T ret;

    size_t pos = 0, next = 0;
    while (true) {
      next = in.find(delim, pos);
      if (next == std::string::npos) break;
      ret.emplace_back(in.substr(pos, next - pos));
      pos = next + 1;
    }
    ret.emplace_back(in.substr(pos));

    return ret;
  }


  /**
   * @brief Strip whitespace from a string.
   * @param in: The mutable string to edit in place.
   */
  inline void strip(std::string& in) {
    while (in[0] == ' ') in.erase(0, 1);
    for (size_t l = in.length() - 1; in[l] == ' '; in.erase(l--, 1));
  }


  #define log(x) if (shared::verbose) std::cout << x << std::endl;


  /**
   * @brief A tree for caching purposes
   * @info The tree is a specialized version of a standard tree, specifically for
   * storing row information, even more specifically for caching. Basically, we break
   * a row down into a tree, where each cell in the row is stored as a node, which branches
   * for the various combinations of the row's cells. At each node, we store a value,
   * which can be anything (Hence the template), but is currently used to cache the score
   * and row matches. Contrast how you would need to store this data in a traditional map:
   *
   * {1} = 1
   * {1,2} = 2
   * {1,3} = 3
   * {1,2,3} = 6
   * {1,2,4} = 8
   * {1,3,4} = 12
   * {1,2,3,4} = 24
   *
   * Compare to a tree:
   *                     1 = 1
   *                    /     \
   *             2 = 2         3 = 3
   *            /     \             \
   *       3 = 6       4 = 8         4 = 12
   *      /
   * 4 = 24
   *
   * The space requirement goes down from 18 to 7 for keys,
   * And searching is worst case equal to the amount of columns,
   * Where a hashmap is technically O(1), at least until you start
   * getting enough values start chaining collisions of the bins.
   */
  template <typename T> class Tree {
    private:

    // A node in the tree
    class Node {
      private:

      // Its name
      std::string key;

      // Its value
      T value = {};

      // Its children
      std::vector<Node> states;

      public:
        Node() = default;
        Node(const std::string_view& k) {key = k;}
        Node(const std::string_view& k, const T& m) {key = k; value = m;}

        /**
         * @brief Recursively add a row to the tree.
         * @param row: The entire row
         * @param v: The value to store for this row
         * @param max: How many columns we should consume, this lets us store a value
         * for only the first two columns of the row, like {1,2} in {1,2,3,4,5}
         * @param x: An internal counter
         */
        void add(const std::vector<std::string_view>& row, const T& v, const size_t& max, const size_t& x) {

          // If we've reached the end, add the value (No collisions should happen).
          if (x == max + 1) {
            if (value != T()) throw std::runtime_error("Tree collision!");
            value = v;
            return;
          }

          // Otherwise, either add the intermediary column, or step into it.
          const auto& val = row[x];
          auto find = std::find(states.begin(), states.end(), val);
          if (find == states.end()) states.emplace_back(val).add(row, v, max, x + 1);
          else find->add(row, v, max, x + 1);
        }

        /**
         * @brief Check if a row exists in the tree.
         * @param row: The row
         * @param max: The upper bound for the columns.
         * @param x: Internal counter.
         */
        bool in(const std::vector<std::string_view>& row, const size_t& max, const size_t& x) const {

          // If we've finished iteration, make sure something exists.
          if (x == max + 1) {
            return value != T();
          }

          // Otherwise find/add child, and recursively call.
          const auto& val = row[x];
          if (states.empty()) return false;
          auto find = std::find(states.begin(), states.end(), val);
          if (find == states.end()) return false;
          return find->in(row, max, x + 1);
        }


        /**
         * @brief Get the value stored for a row
         * @param row: The row
         * @param max: The upper bound for the column
         * @param x: Internal counter
         */
        const T& get(const std::vector<std::string_view>& row, const size_t& max, const size_t& x) const  {

          // At the end, return the value.
          if (x == max + 1) {
            return value;
          }

          // Find/Add; recursive call.
          const auto& val = row[x];
          auto find = std::find(states.begin(), states.end(), val);
          if (states.empty() || find == states.end()) throw std::runtime_error("Value does not exist");
          return find->get(row, max, x + 1);
        }

        // For set operations. I would rather use the integers, but the set doesn't
        // function correctly with them, so we just use the string.
        inline bool operator < (const Node& n) const {return key < n.key;}
        inline bool operator < (const std::string_view& str) const {return key < str;}
        inline bool operator == (const Node& n) const {return key == n.key;}
        inline bool operator == (const std::string_view& str) const {return key == str;}
    };

    Node root;

    // For stats.
    size_t hits = 0;
    size_t misses = 0;

    public:

    Tree() = default;

    // Add
    void add(const std::vector<std::string_view>& row, const T& matches, const size_t& max=SIZE_MAX) {
     root.add(row, matches, (max == SIZE_MAX ? row.size() - 1 : max), 0);
    }

    // Search
    bool in(const std::vector<std::string_view>& row, const size_t& max=SIZE_MAX) {
      auto i = root.in(row, (max == SIZE_MAX ? row.size() - 1 : max), 0);
      if (i) ++hits;
      else ++misses;
      return i;
    }

    // Retrieval
    const T& get(const std::vector<std::string_view>& row, const size_t& max=SIZE_MAX) const {
      return root.get(row, (max == SIZE_MAX ? row.size() - 1 : max), 0);
    }

    // Stats
    const size_t& total_hits() {return hits;}
    const float hit_rate() {return float(hits) / float(hits + misses);}
  };


  /**
   * @brief A integer range
   */
  class Range {
    private:

      // We store the actual values, and the string output so we only need to generate the latter once.
      size_t min = 0, max = 0;
      std::string out;

    public:

      Range() = default;
      Range(Range& r) {min = r.min; max = r.max; out = r.out;}
      Range(const Range& r) {min = r.min; max = r.max; out = r.out;}


      // Construct a range from a min and max.
      Range(const size_t& m, const size_t& M) {
        if (m < M) {min = m; max = M;}
        else {min = M; max = m;}
        update_string();
      }

      Range(const std::string& s) {
        assert(s[0] == '[' && s[s.length()-1] == ']');
        const auto trimmed = s.substr(1, s.length() - 2);
        const auto minmax = split(trimmed, "-");
        assert(minmax.size() == 2);
        min = std::stoi(minmax[0]); max = std::stoi(minmax[1]);
        out = s;
      }

      // Get the string.
      const std::string& str() const {return out;}

      // Check if a integer value exists within the range
      inline bool in(const size_t& val) const {return val >= min && val <= max;}
      inline bool in(const Range& val) const {return val.min >= min && val.max <= max;}

      // Get the range of values.
      inline size_t range() const {return max - min;}

      // Conditional updates to widen bounds.
      void update_min(const size_t& m) {if (m < min || min == max) min = m;}
      void update_max(const size_t& M) {if (M > max || min == max) max= M;}
      void update(const Range& r) {update_min(r.min); update_max(r.max); update_string();}
      void update_string() {
        std::stringstream out_stream;
        out_stream << '[' << min << '-' << max << ']';
        out = out_stream.str();
      }


      // For set operations. I would rather use the integers, but the set doesn't
      // function correctly with them, so we just use the string.
      inline bool operator < (const Range& r) const {return out < r.out;}
      inline bool operator == (const Range& r) const {return out == r.out;}
  };


  // Might as well have something to look at while the user waits.
  static const std::vector<std::string>
  mg = {
    " /$$      /$$ /$$            /$$$$$$                    ",
   "| $$$    /$$$|__/           /$$__  $$                    ",
   "| $$$$  /$$$$ /$$ /$$$$$$$ | $$  \\__/  /$$$$$$  /$$$$$$$ ",
   "| $$ $$/$$ $$| $$| $$__  $$| $$ /$$$$ /$$__  $$| $$__  $$",
   "| $$  $$$| $$| $$| $$  \\ $$| $$|_  $$| $$$$$$$$| $$  \\ $$",
   "| $$\\  $ | $$| $$| $$  | $$| $$  \\ $$| $$_____/| $$  | $$",
   "| $$ \\/  | $$| $$| $$  | $$|  $$$$$$/|  $$$$$$$| $$  | $$",
   "|__/     |__/|__/|__/  |__/ \\______/  \\_______/|__/  |__/",
  },
  ga = {
  "  /$$$$$$                                  /$$     /$$          ",
  " /$$__  $$                                | $$    |__/          ",
  "| $$  \\__/  /$$$$$$  /$$$$$$$   /$$$$$$  /$$$$$$   /$$  /$$$$$$$",
  "| $$ /$$$$ /$$__  $$| $$__  $$ /$$__  $$|_  $$_/  | $$ /$$_____/",
  "| $$|_  $$| $$$$$$$$| $$  \\ $$| $$$$$$$$  | $$    | $$| $$      ",
  "| $$  \\ $$| $$_____/| $$  | $$| $$_____/  | $$ /$$| $$| $$      ",
  "|  $$$$$$/|  $$$$$$$| $$  | $$|  $$$$$$$  |  $$$$/| $$|  $$$$$$$",
  " \\______/  \\_______/|__/  |__/ \\_______/   \\___/  |__/ \\_______/"
  };


  static constexpr char
    END[] = "\e[0m", RED[] = "\e[31m", YELLOW[] = "\e[1;33m",
    GREEN[] = "\e[32m", BLUE[] = "\e[34m", VIOLET[] = "\e[35m";


  static int mode = 0;
  static void pick_mode() {
    auto old = mode;
    do {
      std::uniform_int_distribution<std::mt19937::result_type> mode_dist(1,6);
      mode = mode_dist(rng);
    } while (mode == old);
  }


  static std::string print_title(const std::vector<std::string>& title, const uint64_t& cycle) {
    std::stringstream ret;
    std::vector<std::string> lookup = {RED, YELLOW, GREEN, BLUE, VIOLET};

    for (size_t x = 0; x < title.size(); ++x) {
      for (size_t y = 0; y < title[x].length(); ++y) {
        size_t h = title[x].length(), v = title.size();
        switch (mode) {
          case 0: break;
          case 1: ret << lookup[(x + y + cycle) % 5]; break;
          case 2: ret << lookup[(x + cycle) % 5]; break;
          case 3: ret << lookup[(y + cycle) % 5]; break;
          case 4: ret << lookup[((v - x) + (h - y) + cycle) % 5]; break;
          case 5: ret << lookup[((v - x) + cycle) % 5]; break;
          case 6: ret << lookup[((h - y) + cycle) % 5]; break;
          default: break;
        }
        ret << title[x][y];
      }
      ret << END << '\n';
    }
    return ret.str();
  }
}
