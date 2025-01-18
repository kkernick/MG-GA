#include <chrono>   // For seeding.

#include "mg.h"     // For MinGen
#include "ga.h"     // For GeneticAlgorithm
#include "shared.h"


std::string help =
"main [--mode=m] [--input=filename] [--sensitivities=q,q,q,...] (--domains=filename) (--delim=delimiter) (--types=s,s,s,...)\n"
"(--weights=1,1,1,1) (--metric=md) (--k=2) (--iterations=-1) (--population=100) (--mutation-rate=10) (--single-thread) (--no-cache) (--help)\n"
" General settings\n"
"     --mode/-e             What mode to use. Either MinGen (mg) or Genetic (ga).\n"
"     --input/-i            The table.\n"
"     --sensitivities/-s    The sensitivities for each column. q=quasi, i=ignored, s=sensitive. Defaults to quasi\n"
"     --domains/-h          The path to the domains/hierarchy file.\n"
"     --delim/-d            The delimiter used in the input file. Defaults to automatic detection.\n"
"     --types/-t            The types for each column. s=string, i=integer. Defaults to string.\n"
"     --weights/-w          The weights for each column. Defaults to 1\n"
"     --metric/-m           The scoring metric. c=certainty, md=minimal distortion. Defaults to md,\n"
"     --k/-k                The k value for k-anonymity. Defaults to 2.\n"
"     --single-thread       Run single-threaded. This disables the dynamic progress screen.\n"
"     --no-cache            Disable the metric caches\n"
"     --help/-help          Display this message.\n"
" Options for --mode=mg\n"
"     --iterations/-r       The max amount of states to iteration over. Defaults to -1 (Exhaustive for MinGen,1000 for Genetic)\n"
" Options for --mode=ga\n"
"     --population/-p       The size of each generation. Defaults to 1000\n"
"     --mutation-rate       The probability for a mutation to occur. Defaults to 10.\n"
"Column configuration is structured as a list of value separated by a comma without whitespace, such as q,q,q.\n"
"The list does not need to be complete, missing values will be filled with the default, but is read 0th column to nth column.\n"
"Weights can be any floating point number (eg. 1,-10,50000), there is no limit on precision save limitations of the system float.\n"
"Higher weights add a multiplicative burden for changes, and dissuade the algorithm from changing the value, preserving utility.\n"
"Mutation rate is added to a 0-100 roll, with any value over 100 causing a mutation. For a rate of 10, we roll 0-110.\n";


int main(int argc, char* argv[]) {

  // Seed
  shared::rng.seed(std::chrono::system_clock::now().time_since_epoch().count());

  // All the variables the user can customize on the command line.
  std::string filename, domains_file, delim="", types="", weights="", sensitivities="", mode="";
  metric::metric m = metric::md;
  size_t k = 2, max = -1, population = 100, mutation_rate=10;

  // Handle command line arguments.
  for (const auto& x : std::vector<std::string>(argv + 1, argv + argc)) {
    auto split = x.find("=");
    if (split == std::string::npos) {
      if (x == "--verbose" || x == "-v") shared::verbose = true;
      else if (x == "--no-cache" || x == "-c") shared::cache = false;
      else if (x == "--single-thread") shared::single_thread = true;
      else if (x == "--help" || x == "-h") {std::cout << help << std::endl; return 0;}
    }

    else {
      auto key = x.substr(0, split), value = x.substr(split + 1);
      if (key == "--input" || key == "-i") filename = value;
      else if (key == "--mode" || key == "-e") mode = value;
      else if (key == "--domains" || key == "-h") domains_file = value;
      else if (key == "--delim" || key == "-d") delim = value;
      else if (key == "--types" || key == "-t") types = value;
      else if (key == "--weights" || key == "-w") weights = value;
      else if (key == "--sensitivities" || key == "-s") sensitivities = value;
      else if (key == "--metric" || key == "-m") {if (value == "c") m = metric::c;}
      else if (key == "--k" || key == "-k") k = std::stoi(value);
      else if (key == "--iterations" || key == "-r") max = std::stoi(value);
      else if (key == "--population" || key == "-p") population = std::stoi(value);
      else if (key == "--mutation-rate") mutation_rate = std::stoi(value);
      else {std::cerr << "Unrecognized argument: " << x << ". See --help for help." << std::endl; return 1;}
    }
  }

  // Make sure the file exists.
  if (mode.empty() || filename.empty() || sensitivities.empty()) {
    std::cerr << "Filename, Mode, and Sensitivies is required. See --help for details" << std::endl;
   return 1;
  }

  // Constructs the domains, if they exist.
  auto domains = domain::Domain::construct(domains_file);
  auto data = table::Table(filename, domains, delim, types, weights, sensitivities);

  if (mode == "mg") {
    auto worker = mg::MinGen(data);
    worker.anonymize(k, m, max);
  }
  else if (mode == "ga") {
    auto worker = ga::GeneticAlgorithm(data);
    worker.anonymize(k, m, max, population, mutation_rate);
  }
  else throw std::runtime_error("Invalid mode!");
  return 0;
}
