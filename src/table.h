/*
 * This file contains the Table class, which is used as an indexed representation
 * of a CSV/TSV
 */

#pragma once

#include <iomanip>    // For setting width
#include <map>        // For maps
#include <set>        // Sets

#include "shared.h"
#include "domains.h"


// Forward declaration for friendship.
// Circular Dependencies? Such a thing doesn't exist in C++
namespace mg {class MinGen;}
namespace ga {class GeneticAlgorithm;}

/**
 * @brief The table namespace
 */
namespace table {

  // Column types
  typedef enum {string, integer} type;

  /**
   * @brief Lookup a type value from a string.
   * @param x: The string, either s, i, or f.
   * @returns The type value.
   */
  inline type static t_lookup(const std::string& x) {
    if (x == "s") return string;
    else if (x == "i") return integer;
    throw std::runtime_error("Unrecognized type: " + x);
  }


  typedef enum {ignore, quasi, sensitive} classification;


  /**
   * @brief Lookup a sensitivity value from a string.
   * @param x: The string, either i, q, or s.
   * @returns The type value.
   */
  inline classification static s_lookup(const std::string& x) {
    if (x == "i") return ignore;
    else if (x == "q") return quasi;
    else if(x == "s") return sensitive;
    throw std::runtime_error("Unrecognized sensitivity: " + x);
  }


  /**
   * @brief a CSV/TSV Table file
   */
  class Table {

    // Let MinGen access private members
    friend mg::MinGen;
    friend ga::GeneticAlgorithm;

    private:

    // The list of column names.
    std::vector<std::string> header;
    size_t rows = 0;

    // Define the type of data stored in a column
    // We store a lot of various data to each column,
    // for speed and convenience.
    typedef struct column {

      // What kind of column
      type t = string;

      // The weight of data in the column for scoring
      float weight = 1.0;

      classification sensitivity = quasi;

      // The maximal width of any entry, for table rendering.
      size_t width = 0;

      // A set of unique values in the column, used for range calculations.
      std::set<std::string> unique = {};

      // A list of all possible pairs of integers.
      std::set<shared::Range> ranges = {};
      shared::Range range = shared::Range(0, 0);

      // A domain graph, if any.
      domain::Domain graph = domain::Domain();

      // The list of data.
      std::vector<std::string> data;
    } column;

    // Columns
    std::map<std::string, column> data;


    // Get an index from the map.
    const column& index(const size_t& index) const {return data.at(header[index]);}
    column& index(const size_t& index) {return data.at(header[index]);}



    /**
     * @brief Generate a list of all possible mutations for a given cell.
     * @param value: The value of the cell.
     * @param col: The column that the value is contained in.
     * @param randomize: For non-exhaustive search, don't deterministically choose mutations.
     * @returns A set of all string mutations.
     *
     */
    std::vector<std::string> static mutations(const std::string& value, const column& col, const bool& randomize=false) {
      std::vector<std::string> ret;

      // Suppress
      ret.emplace_back("*");

      // Options from the domain
      if (!col.graph.empty()) {
        const auto derived = col.graph.find(value);
        for (const auto& x : derived) ret.emplace_back(x);
      }

      // THe value itself (Included in the domain, hence the conditional)
      else if (!value.empty()) {ret.emplace_back(value);}

      // Ranges
      if (col.t == type::integer) {

        // If the value is already a range, compare it to other ones.
        if (value[0] == '[') {
          const auto r = shared::Range(value);
          for (const auto& range : col.ranges) {
            if (range.in(r)) ret.emplace_back(range.str());
          }
        }
        else {
          const auto number = std::stoi(value);
          for (const auto& range : col.ranges) {
            if (range.in(number)) ret.emplace_back(range.str());
          }
        }
      }

      // Return
      if (randomize) std::ranges::shuffle(ret, shared::rng);
      return ret;
    }


    /**
     * @brief Generate the set of all possible ranges for numerical columns.
     * @param col: The column
     * @info This function simply runs a O(n**2) double-iteration to find
     * all possible combinations of numerical values; by using a set; redundant
     * operations are ignored.
     */
    void generate_ranges(column& col) {
      if (col.t != integer) {
        col.range = shared::Range(0, col.unique.size());
      }
      else {
        for (const auto& x : col.data) {
          for (const auto& y : col.data) {
            if (x == y || x == "*" || y == "*") continue;

            shared::Range range;
            if (x[0] == '[') range = shared::Range(x);
            else if (y[0] == '[') range = shared::Range(y);
            else range = shared::Range(std::stoi(x), std::stoi(y));

            col.ranges.emplace(range);
            col.range.update(range);
          }
        }
      }
      col.range.update_string();
      col.ranges.erase(col.range);
    }

    public:

    /**
     * @brief An iterator for row-by-row traversal.
     * @info The Table is a map of vectors, which means that rows exist as
     * values with the same index scattered amongst the vectors. The RowIterator
     * exists as an abstraction to iterate between these values.
     * @info The view, as the name suggests returns a set of strings; this means
     * that we don't actually take ownership of the strings (And in effect copy it),
     * but instead present the value from the vectors. Coupled with a view that is
     * initialized and re-used, and the RowIterator makes no copies in traversal. The
     * downside of this is that you can't modify the values in the RowIterator, but we
     * never use it for modification anyways.
     * @info The nice thing about C++ is that the standard library plays nice with
     * custom classes as long as you define the proper operations. We only need
     * a dereference operator (*), and a increment operator (++) to turn a class into
     * a forward iterator, which means that we can use the nice for loop of:
     * for (const auto& : table), or, we can use the expanded version:
     * for (auto i = begin(); i != end(); ++i)
     */
    class RowIterator {
      private:

        // Store a reference to the table we're iterating.
        const Table& t;

        // Our view uses strings to prevent copying, and is reused itself.
        std::vector<std::string_view> view = {};

        // The current row.
        size_t row = 0;

        // Update the view for the current row.
        void construct_view() {for (size_t x = 0; x < t.header.size(); ++x) view[x] = t.index(x).data[row];}

      public:

      /**
       * @brief Construct a RowIterator
       * @param in: The Table
       * @param r: The row.
       */
      RowIterator(const Table& in, const size_t& r = 0): t(in) {
        assert(r <= t.rows);
        row = r;

        // Construct the view vector, and update it.
        for (size_t x = 0; x < t.header.size(); ++x) view.emplace_back();
        if (row != t.rows) construct_view();
      }

      // Get the current view
      const std::vector<std::string_view>& get() const {return view;}
      const std::vector<std::string_view>& operator * () const {return view;}

      // Move the iterator to the next row.
      RowIterator& inc() {
        if (row == t.rows) throw std::out_of_range("Exceeded size of Table!");
        ++row;
        construct_view();
        return *this;
      }
      RowIterator& operator ++ () {return inc();}

      // Get the row
      const size_t& current() const {return row;}

      // Compare. Don't use get() on row_end(), because the view isn't defined.
      friend bool operator== (const RowIterator& a, const RowIterator& b) { return a.row == b.row; };
      friend bool operator!= (const RowIterator& a, const RowIterator& b) { return a.row != b.row; };
    };

    // Row iteration.
    RowIterator row_begin() const {return RowIterator(*this, 0);}
    RowIterator row_end() const {return RowIterator(*this, rows);}
    RowIterator row_begin() {return RowIterator(*this, 0);}
    RowIterator row_end() {return RowIterator(*this, rows);}

    // Column iteration can just use the maps iterator.
    // auto col_begin() {return data.begin();}
    auto col_end() const {return data.end();}
    auto col_begin() const {return data.begin();}
    auto col_end() {return data.end();}

    Table() = default;

    /**
     * @brief Construct a Table
     * @param filename: The table file
     * @param domains: The domains.
     * @param delim: The delimiter used in the table to separate columns. An empty string will guess.
     * @param types_string: A list of type characters separated by commas. An empty string will guess.
     */
    Table(
      const std::string& filename,
      const std::vector<domain::Domain>& domains={},
      std::string delim="",
      const std::string& types_string="",
      const std::string& weights_string="",
      const std::string& s_string=""
    ) {

      // Open the file
      auto file = std::ifstream(filename);
      if (!file.is_open())
        throw std::runtime_error("Failed to read file");
      std::string line;

      // Get the header
      std::getline(file, line);

      // If there is no delimiter, guess.
      if (delim.empty()) {
        log("Guessing delimiter. You can use --delim to explicitly provide one.");
        for (const auto& d : {"\t", " ", ","}) {
          if (line.find(d) != std::string::npos) {
            log("Assuming delimiter is: " << (d[0] == '\t' ? "tab" : d));
            delim = d;
            break;
          }
        }
      }

      // Split the header, create the map.
      header = shared::split(line, delim);
      for (const auto& x : header) {
        data[x] = column{.width = x.length()};
        for (const auto& domain : domains) {
          if (domain.name() == x) {
            data[x].graph = domain;
            log("Embedded domain hierarchy for " << x);
            break;
          }
        }
      }

      // Get the types for each column.
      auto type_vec = types_string.empty() ? std::vector<std::string>() : shared::split(types_string, ",");
      if (type_vec.size() < header.size()) {
        log("Missing types in the provided table are assumed to be strings. You can use --types to explicitly provide them.");
        while (header.size() >= type_vec.size()) type_vec.emplace_back("s");
      }
      else if (type_vec.size() > header.size()) log("Redundant types will be ignored");
      for (size_t x = 0; x < header.size(); ++x) data[header[x]].t = t_lookup(type_vec[x]);

      // Get weights
      auto weight_vec = weights_string.empty() ? std::vector<std::string>() : shared::split(weights_string, ",");
      if (weight_vec.size() < header.size()) {
        log("Missing weights in the provided table are assumed to be 1.0. You can use --weights to explicitly provide them.");
        while (header.size() >= weight_vec.size()) weight_vec.emplace_back("1.0");
      }
      else if (weight_vec.size() > header.size()) log("Redundant weights will be ignored");
      for (size_t x = 0; x < header.size(); ++x)
        data[header[x]].weight = std::stof(weight_vec[x]);

      // Get classification
      auto class_vec = s_string.empty() ? std::vector<std::string>() : shared::split(s_string, ",");
      if (class_vec.size() < header.size()) {
        log("Missing sensitivies in the provided table are assumed to be quasi. You can use --sensitivities to explicitly provide them.");
        while (header.size() >= class_vec.size()) class_vec.emplace_back("q");
      }
      else if (class_vec.size() > header.size()) log("Redundant sensitivities will be ignored");
      for (size_t x = 0; x < header.size(); ++x)
        data[header[x]].sensitivity = s_lookup(class_vec[x]);

      // Populate the rest of the table.
      while (std::getline(file, line)) {
        auto line_split = shared::split(line, delim);
        for (size_t x = 0; x < header.size(); ++x) {
          auto& col = data[header[x]];
          const auto& name = line_split[x];

          // If there is a graph, validate the data. Note that this is just a warning.
          // You don't need to have all your data strictly adhere to your domains, it just means
          // that data cannot be inferred and mutated.
          if (!col.graph.empty()) {
            if (col.graph.find(name).empty()) {
              log("Validation Warning: " << line_split[x] << " does not exist in domain graph: " << col.graph.name());
            }
          }

          // Add the data to the vector to preserve the order, then "add" it to the unique set,
          // with it being discarded if it already exists.
          col.data.emplace_back(name);
          col.unique.emplace(name);
          col.width = std::max(col.width, name.length());
        }
        ++rows;
      }

      // Get ranges
      for (auto& col : data) generate_ranges(col.second);
      file.close();
    }


    /**
     * @brief Print the table with Markdown format.
     */
    void print() const {

      // Calculate the total width of the table for the # string.
      size_t width = data.size() + 2;

      // Print the headers.
      for (size_t x = 0; x < header.size(); ++x) {
        std::cout << std::internal << "| " << std::setw(index(x).width) << header[x] << " ";
        width += index(x).width + 2;
      }
      std::cout << " |\n" << std::string(width, '#') << std::endl;

      // Print the values row by row.
      for (auto iter = row_begin(); iter != row_end(); ++iter) {
        const auto& view = iter.get();
        for (size_t x = 0; x < header.size(); ++x) {
          std::cout << std::internal << "| " << std::setw(index(x).width) << view[x] << " ";
        }
        std::cout << " |" << std::endl;
      }
    }


    /**
     * @brief Get the distinct permutations of the table
     */
    size_t get_distinct() const {

      size_t total = 1;
      for (const auto& col : data) {
        for (const auto& e : col.second.data) {

          // Get mutations, multiply them
          if (shared::verbose) std::cout << e << ": ";
          auto mut = mutations(e, col.second);
          total *= mut.size();

          if (shared::verbose) {
            for (const auto& x : mut) {
              std::cout << "'" <<  x << "' ";
            }
            std::cout << ": " << mut.size() << std::endl;
          }
        }
      }
      if (header.size() * data.at(header[0]).data.size() > 64) total = SIZE_MAX;
      if (shared::verbose) {
        std::cout << "TOTAL: ";
        if (total == SIZE_MAX) std::cout << "OVERFLOW";
        else std::cout << total;
        std::cout << std::endl;
      }
      return total;
    }


    /**
     * @brief Generate a random table given all possible mutations of a cell.
     * @returns The table.
     */
    Table random() const {
      Table copy = *this;
      for (size_t col = 0; col < header.size(); ++col) {
        auto& column = copy.get_column(col);
        if (column.sensitivity != quasi) continue;
        for (size_t row = 0; row < column.data.size(); ++row) {
          column.data[row] = copy.mutations(column.data[row], column, true)[0];
        }
      }
      return copy;
    }

    // Get the amount of columns
    const size_t columns() const {return header.size();}

    // Get a cell from an index
    const std::string& index(const size_t& r, const size_t& c) const {return data.at(header[c]).data[r];}
    std::string& index(const size_t& r, const size_t& c) {return data.at(header[c]).data[r];}


    // Get the column directly
    const column& get_column(const size_t& c) const {return data.at(header[c]);}
    column& get_column(const size_t& c) {return data.at(header[c]);}

    const bool operator == (const Table& o) const {
      for (size_t col = 0; col < columns(); ++col) {
        const auto& c1 = get_column(col), c2 = o.get_column(col);
        for (size_t row = 0; row < c1.data.size(); ++row) {
          if (c1.data[row] != c2.data[row]) return false;
        }
      }
      return true;
    }

    /**
     * @brief Update the widths of columns.
     * @info We use this rather than updating width at each cell change, which allows the
     * intermediary tables (Which we never actually print), to not bother with keeping widths
     * accurate until we actually need to display it.
     */
    void update_widths() {
      for (auto& column : data) {
        for (const auto& cell : column.second.data) {
          column.second.width = std::max(column.second.width, cell.length());
        }
      }
    }

    // For multiset comparisons. Just compare the first character of the first row of the first column.
    // This just gives the set a metric to which it can compare and speed up emplacement.
    bool operator < (const Table& s) const {return index(0).data[0][0] < s.index(0).data[0][0];}
  };
}
