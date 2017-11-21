#include <algorithm>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <unistd.h>
#include "memory.h"
#include "cpu.h"

extern char   *optarg;
extern int32_t optind;
extern int32_t optopt;
extern int32_t opterr;
extern int32_t optreset;

using namespace std;

// Usage of the program
static void usage(char *name)
{
	cout << name << " usage:\n" <<
          "\t-b type_of_branch_predictor: 1-bit predictor(1) or 2-bit predictor(2)\n" <<
          "\t-e the_number_of_btb_entries: the number of branch target buffer entries\n" <<
	        "\t-t text_stream_file: load .text with the contents of file\n" <<
	        "\t-d data_stream_file: [optional] load .data with contents of file\n" <<
	        "\t-v: very verbose single-click CPU (echo every stage, pause after each cycle)\n";
}


// *****************************
//           entry point
// *****************************
int32_t main(int32_t argc, char **argv)
{
	memory   mem;
	bool     verb = false;
	bool     text_loaded = false;
	bool     show_cycles = false;
	int32_t  ch;
	uint32_t text_ptr = text_segment;
	uint32_t data_ptr = data_segment;

  int type_branch_predictor = 0;
  int num_btb_entries = 0;

	while ((ch = getopt(argc, argv, "t:d:b:e:vmc")) != -1) {
		switch (ch) {
    case 'b': 
      {
        sscanf(optarg, "%d", &type_branch_predictor);
        if(type_branch_predictor != 1 && type_branch_predictor != 2) {
          cout << "Wrong type of branch predictor! (" << optarg << ")\n";
          exit(20);
        }
      }
      break;
    case 'e': 
      {
        int valid_btb_entries[] = {8, 16, 32, 64, 128, 256, 512, 1024};
        int invalid = 1;
        sscanf(optarg, "%d", &num_btb_entries);
        for(int i=0; i<sizeof(valid_btb_entries); i++) {
          if(valid_btb_entries[i] == num_btb_entries) {
            invalid = 0;
          }
        }
        if(invalid == 1) {
          cout << "Wrong number of branch predictor entries! (" << optarg << ")\n";
          exit(20);
        }
      }
      break;
		case 't': {
			ifstream input(optarg, ios::binary);
			if (!(input.good() && input.is_open())) {
				cout << *argv << ": " << optarg << " does not exist" << endl;
				exit(20);
			}
			byte  c;
			char *pc = (char *)&c;
			while (!input.eof()) {
				input.read(pc, sizeof(byte));
				mem.set<byte>(text_ptr++, c);
			}
			input.close();
			text_loaded = true;
		}
		break;

		case 'm':
			mem.collect_stats(true);
			break;

		case 'c':
			show_cycles = true;
			break;

		case 'd': {
			ifstream input(optarg, ios::binary);
			if (!(input.good() && input.is_open())) {
				cout << *argv << ": " << optarg << " does not exist" << endl;
				exit(20);
			}
			byte c;
			while (!input.eof()) {
				input.read((char *)&c, sizeof(byte));
				mem.set<byte>(data_ptr++, c);
			}
			input.close();
		}
		break;

		case 'v':
			verb = true;
			break;

		default:
			usage(*argv);
			exit(10);
			break;
		}
	}

	if (!text_loaded) {
		usage(*argv);
		exit(10);
	}

  cout <<"Type of branch predictor: " << type_branch_predictor << "-bit predictor\n";
  cout << "The number of BTB entries: " << num_btb_entries << " entries\n";

	cout << *argv << ": Starting CPU..." << endl;
	run_cpu(&mem, verb, type_branch_predictor, num_btb_entries);
	cout << *argv << ": CPU Finished" << endl;

	if (mem.is_collecting()) mem.display_memory_stats();
}
