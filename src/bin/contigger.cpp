//(c) 2016 by Authors
//This file is a part of ABruijn program.
//Released under the BSD license (see LICENSE file)

#include <iostream>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <cmath>
#include <execinfo.h>

#include "../sequence/sequence_container.h"
#include "../common/config.h"
#include "../common/logger.h"
#include "../common/utils.h"
#include "../common/memory_info.h"

#include "../repeat_graph/repeat_graph.h"
#include "../repeat_graph/read_aligner.h"
#include "../repeat_graph/output_generator.h"
#include "../contigger/contig_extender.h"

#include <getopt.h>

bool parseArgs(int argc, char** argv, std::string& readsFasta, 
			   std::string& outFolder, std::string& logFile, 
			   std::string& inAssembly, int& kmerSize,
			   int& minOverlap, bool& debug, size_t& numThreads, 
			   std::string& configPath, std::string& inRepeatGraph,
			   std::string& inReadsAlignment)
{
	auto printUsage = [argv]()
	{
		std::cerr << "Usage: " << argv[0]
				  << "\tin_assembly reads_files out_folder config_path "
				  << "repeat_graph reads_alignment\n\t"
				  << "[-l log_file] [-t num_threads] [-v min_overlap]\n\t"
				  << "[-d]\n\n"
				  << "positional arguments:\n"
				  << "\tin_assembly\tpath to input assembly\n"
				  << "\treads_files\tcomma-separated list with reads\n"
				  << "\tout_assembly\tpath to output assembly\n"
				  << "\tconfig_path\tpath to config file\n"
				  << "\trepeat_graph\tpath to repeat graph file\n"
				  << "\treads_alignment\tpath to reads alignment file\n"
				  << "\noptional arguments:\n"
				  << "\t-k kmer_size\tk-mer size [default = 15] \n"
				  << "\t-v min_overlap\tminimum overlap between reads "
				  << "[default = 5000] \n"
				  << "\t-d \t\tenable debug output "
				  << "[default = false] \n"
				  << "\t-l log_file\toutput log to file "
				  << "[default = not set] \n"
				  << "\t-t num_threads\tnumber of parallel threads "
				  << "[default = 1] \n";
	};

	numThreads = 1;
	debug = false;
	minOverlap = -1;
	kmerSize = 15;

	const char optString[] = "l:t:v:k:hd";
	int opt = 0;
	while ((opt = getopt(argc, argv, optString)) != -1)
	{
		switch(opt)
		{
		case 't':
			numThreads = atoi(optarg);
			break;
		case 'v':
			minOverlap = atoi(optarg);
			break;
		case 'k':
			kmerSize = atoi(optarg);
			break;
		case 'l':
			logFile = optarg;
			break;
		case 'd':
			debug = true;
			break;
		case 'h':
			printUsage();
			exit(0);
		}
	}
	if (argc - optind != 6)
	{
		printUsage();
		return false;
	}

	inAssembly = *(argv + optind);
	readsFasta = *(argv + optind + 1);
	outFolder = *(argv + optind + 2);
	configPath = *(argv + optind + 3);
	inRepeatGraph = *(argv + optind + 4);
	inReadsAlignment = *(argv + optind + 5);

	return true;
}

int main(int argc, char** argv)
{
	#ifndef _DEBUG
	signal(SIGSEGV, segfaultHandler);
	std::set_terminate(exceptionHandler);
	#endif

	bool debugging = false;
	size_t numThreads = 1;
	int kmerSize = 15;
	int minOverlap = 5000;
	std::string readsFasta;
	std::string inAssembly;
	std::string inRepeatGraph;
	std::string inReadsAlignment;
	std::string outFolder;
	std::string logFile;
	std::string configPath;
	if (!parseArgs(argc, argv, readsFasta, outFolder, logFile, inAssembly,
				   kmerSize, minOverlap, debugging, 
				   numThreads, configPath, inRepeatGraph, 
				   inReadsAlignment))  return 1;
	
	Logger::get().setDebugging(debugging);
	if (!logFile.empty()) Logger::get().setOutputFile(logFile);
	Logger::get().debug() << "Build date: " << __DATE__ << " " << __TIME__;
	std::ios::sync_with_stdio(false);
	
	Logger::get().debug() << "Total RAM: " 
		<< getMemorySize() / 1024 / 1024 / 1024 << " Gb";
	Logger::get().debug() << "Available RAM: " 
		<< getFreeMemorySize() / 1024 / 1024 / 1024 << " Gb";
	Logger::get().debug() << "Total CPUs: " << std::thread::hardware_concurrency();

	
	Config::load(configPath);
	Parameters::get().numThreads = numThreads;
	Parameters::get().kmerSize = kmerSize;
	Logger::get().debug() << "Running with k-mer size: " << 
		Parameters::get().kmerSize; 

	Logger::get().info() << "Reading sequences";
	SequenceContainer seqAssembly; 
	SequenceContainer seqReads;
	std::vector<std::string> readsList = splitString(readsFasta, ',');
	try
	{
		seqAssembly.loadFromFile(inAssembly);
		for (auto& readsFile : readsList)
		{
			seqReads.loadFromFile(readsFile);
		}
	}
	catch (SequenceContainer::ParseException& e)
	{
		Logger::get().error() << e.what();
		return 1;
	}
	seqReads.buildPositionIndex();
	seqAssembly.buildPositionIndex();

	RepeatGraph rg(seqAssembly);
	rg.loadGraph(inRepeatGraph);
	ReadAligner aln(rg, seqAssembly, seqReads);
	aln.loadAlignments(inReadsAlignment);
	OutputGenerator outGen(rg, aln, seqAssembly, seqReads);

	//Logger::get().info() << "Generating contigs";

	ContigExtender extender(rg, aln, seqAssembly, seqReads);
	extender.generateUnbranchingPaths();
	extender.generateContigs();
	extender.outputContigs(outFolder + "/graph_paths.fasta");
	extender.outputStatsTable(outFolder + "/contigs_stats.txt");
	extender.outputScaffoldConnections(outFolder + "/scaffolds_links.txt");

	outGen.dumpRepeats(extender.getUnbranchingPaths(),
					   outFolder + "/repeats_dump.txt");
	outGen.outputDot(extender.getUnbranchingPaths(),
					 outFolder + "/graph_final.gv");
	outGen.outputFasta(extender.getUnbranchingPaths(),
					   outFolder + "/graph_final.fasta");
	outGen.outputGfa(extender.getUnbranchingPaths(),
					 outFolder + "/graph_final.gfa");

	Logger::get().debug() << "Peak RAM usage: " 
		<< getPeakRSS() / 1024 / 1024 / 1024 << " Gb";

	return 0;
}
