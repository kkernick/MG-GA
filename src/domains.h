/*
 * This file contains functions related to domain graphs.

 * Domains are represented as trees, with the area itself, like "Job"
 * acting as the root node. This identifier must match a column name for
 * domain data to be embedded into tables.
 *
 * A hierarchy is then generated, which has an unbound complexity. For example,
 * The root node may branch off into "Blue Collar" and "High Collar" which may then
 * branch into the individual fields, or another classification followed by more fields.
 * There can be an arbitrary amount of leaves on each tree, with no limit on depth. When
 * parsing, the tree will be traveressed and generate the path from root to the node,
 * which gives the program all the possible classifications of a node. For example,
 * If we wanted to find "Mechanic" we would return Job/Blue Collar/Mechanic, to which
 * any of these values could technically be used in place (Although we omit the root
 * node because it's so general we can just suppresss the value entirely).
 *
 * Constructing a domain from a file follows the above format, placed into a text file
 * (The extension is irrelevant), with each line being a node and its relationships.
 * You need not specify non-leaves, nor domain names themselves, as they will automatically
 * be generated--look at domains.txt in the example folder.
 *
 * The only limitation on this system is that nodes must have unique names in the tree; if
 * you create two nodes with the name name on different branches, the one found first in
 * iteration will always be returned (Which makes sense, the getter function only asks for a
 * node name).
 *
 * While this implementation did not reference the MinGen reference, available here:
 * https://dataprivacylab.org/dataprivacy/projects/kanonymity/kanonymity2.pdf
 * It uses the same general tree for Domains by coincidence, and because its the most obvious and intuitive
 * solutions. There are some visualizations of this structure that may be beneficial there if
 * the code is too dense to parse.
 */

#pragma once

#include <list>     // For linked lists
#include <fstream>  // For files

#include "shared.h"


/**
 * @brief The domain namespace
 */
namespace domain {

   /**
    * @brief A domain.
    * @info The domain is represented as a simple tree. A root node acts
    * as the column name, and the hierarchy is stored as leaves
    * @warning According to this implementation, nodes must have a unique name
    * within the domain. This makes finding a lot easier.
    */
  class Domain {
    private:

    // The value of the current node.
    std::string value;

    // The size, for convenience.
    size_t s = 0;

    // Yes, this just works.
    // You would think that Domain having a member variable that uses itself
    // would cause a dependency cycle, or that it couldn't be properly defined,
    // but turns out you don't need to worry about pointers or dynamic memory at all
    // if you just make a node's children a vector of more nodes.
    std::vector<Domain> children;


    /**
    * @brief Find a node
    * @param child: The name of the node.
    * @param stack: The return stack that is built through the recursive calls.
    * @returns Whether the node exists.
    * @info A string_view is a string in every aspect except that it doesn't
    * own the memory to which it's character array is stored at. The idea is that
    * you have a regular std::string, and rather than creating a copy of it, you
    * instead give a view of the string, such that you can read the data without expensive
    * copies; while you could usually just pass a constant reference to avoid copies, a string_view
    * possesses the trait Trivial Copiable, which means exactly what you think. The reason we use it
    * is because the table::Table's Row Iterator is presenting the appearance of rows when the actual
    * structure is not connected, and hence we need trivial copies to put together a row vector.
    * This design decision permeates the whole of the project, hence why string_views are so ubiquitous.
    */
    bool find(const std::string_view& child, std::vector<std::string>& stack) const {

      // Go through each child.
      for (const auto& c : children) {

        // If the child is our node, or it has the node, then emplace that node.
        // This recursively calls down to the terminal nodes.
        if (c.value == child || c.find(child, stack)) {
          stack.emplace_back(c.value);
          return true;
        }
      }

      // If we get here, none of the branches contain the node, so return false.
      return false;
    }


    /**
    * @brief Get a child node.
    * @param child: The name of the child node.
    * @returns The domain to which the node is a direct child.
    * @info If the child does not exist, it is created.
    */
    Domain& get(const std::string& child) {
      for (auto& c : children)
        if (c.value == child) return c;
        
      // Increase the total count of nodes, since we need to emplace.
      ++s;
      return children.emplace_back(child);
    }

    public:

    Domain() = default;

    // Construct a domain with the name.
    Domain(const std::string_view& r) {value = r;};


    /**
    * @brief Add a node to the Domain.
    * @param path: The path on the tree. The root is implied.
    * @info We use a linked-list rather than a vector so we can more easily
    * pop the front.
    */
    void add(const std::list<std::string>& path) {

      // Start at the root.
       auto* current = this;

      // Go through each element in the path.
      for (auto p : path) {
        shared::strip(p);

        // "Get" the next node in the path. This creates values as it goes, like mkdir -p.
        current = &current->get(p);
      }
    }


    /**
     * @brief Return the breadth of a Domain.
     * @param The name of a node in the Domain.
     * @return the size
     * @info find() returns a path to a node, which gives us the depth
     * of that node within the Domain, and lets us determine all possible
     * substitutions that can be made. However, when calculating certainty scores,
     * we need to know the breadth of a domain (IE the sub-domain Blue/White Collar
     * has a breadth of 2, since there are two possible choices).
     */
    size_t breadth(const std::string_view& name) const {
      // Go through each child, if the node exists, get the total amount of children.
      for (const auto& c : children) {
        if (c.value == name) return children.size();

        // Recursively try and find sub-domains.
        size_t in = c.breadth(name);
        if (in != 0) return in;
      }
      return 0;
    }


    /**
    * @brief Find a node within the Domain.
    * @param The name of the node.
    * @returns The path, starting from the child itself, up to the root.
    * @warning This implementation relies on nodes having unique values; if they don't,
    * then the first match will be always returned.
    */
    std::vector<std::string> find(const std::string_view& child) const {
      // Make the stack, populate it, and return.
      std::vector<std::string> ret;
      find(child, ret);
      return ret;
    }


    // Print the Domain
    void print(const size_t& level = 0) const {
      std::cout << level << ": " << value << std::endl;
      for (const auto& x : children) x.print(level + 1);
    }


    // Get the name
    std::string& name() {return value;}
    const std::string& name() const {return value;}


    // Check if the domain is empty
    bool empty() const {return value.empty();}


    // Check how many nodes are in the domain.
    const size_t& size() {return s;}


    /**
    * @brief Constructs all the defined domains in a domains file.
    * @param filename: The path to the domains definition.
    * @returns a vector of each domain
    * @info The syntax of a domain file is simply `column/paths/to: a,b,c`,
    * separated by newlines. Intermediary nodes do not need to be
    * created explicitly, just ensure that the first element of the
    * path is the column name.
    */
    static std::vector<Domain> construct(const std::string& filename) {
      // Our list of domains.
      std::vector<Domain> ret;
      if (filename.empty()) return ret;

      // Open the file
      auto file = std::ifstream(filename);
      if (!file.is_open()) throw std::runtime_error("Failed to read file");
      std::string line;

      // A linked list that contains our path
      std::list<std::string> path;

      // Keep going for each rule
      while (std::getline(file, line)) {

        // Ignore empty lines
        if (line.empty()) continue;

        // The syntax is a path separated by /, then a colon to split
        // the keys that should be put in that path. Such as:
        // /a/b/c: 1,2,3,4 will create the nodes 1,2,3,4 at node c.
        auto keypair = shared::split(line, ":");
        path = shared::split<std::list<std::string>>(keypair[0], "/");

        // Get the root by simply popping the front; with a std::vector, this
        // would be O(n) complexity. With a list, it's O(1).
        auto root = path.front();
        shared::strip(root);
        path.pop_front();

        // Either find an existing domain, or make one.
        Domain* d = nullptr;
        for (auto& x : ret) {
          if (x.value == root) {d = &x; break;}
        }
        if (d == nullptr) {d = &ret.emplace_back(root);}

        // Go through each key that should be added to the path
        for (auto x : shared::split(keypair[1], ",")) {

          // Strip whitespace, then simply add it to the path, add
          // it to the domain, and then pop it off the path. Because
          // we're using a linked list, emplace/pop is O(1)
          shared::strip(x);
          path.emplace_back(x);
          d->add(path);
          path.pop_back();
        }
      }
      // Return the completed domains.
      return ret;
    }
  };
}
