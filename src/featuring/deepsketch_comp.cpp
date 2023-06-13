#include <iostream>
#include <vector>
#include <set>
#include <bitset>
#include <map>
#include <cmath>
#include <algorithm>
#include "../compress.h"
#include "./deepsketch.h"
#include "../lz4.h"
#include "xxhash.h"
extern "C" {
	#include "../xdelta3/xdelta3.h"
}
#define INF 987654321
using namespace std;

typedef pair<int, int> ii;

int main(int argc, char* argv[]) {
	int xdeltaSavedSpace = 0;
	unsigned long inputSize = 0;
	if (argc != 4) {
		cerr << "usage: ./ann_inf [input_file] [script_module] [threshold]\n";
		exit(0);
	}
	int threshold = atoi(argv[3]);

	FASTCDC* cdc = new FASTCDC(4096);
	DATA_IO f(argv[1], cdc);
	f.N = 0;
//	f.read_file();
	f.treaverse(argv[1]);
	cout<<"file chunks number: "<<f.N<<endl;
	map<XXH64_hash_t, int> dedup;
	list<ii> dedup_lazy_recipe;
	NetworkHash network(256, argv[2]);

	string indexPath = "ngtindex";
	NGT::Property property;
	property.dimension = HASH_SIZE / 8;
	property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
	property.distanceType = NGT::Index::Property::DistanceType::DistanceTypeHamming;
	NGT::Index::create(indexPath, property);
	NGT::Index index(indexPath);

	ANN ann(20, 128, 16, threshold, &property, &index);

	unsigned long long total = 0;
	f.time_check_start();
	for (int i = 0; i < f.N; ++i) {
		char* blockAddr = std::get<0>(f.trace[i]);
		int blockSize = std::get<1>(f.trace[i]);
		inputSize += blockSize;
//		printf("Block number:%d, block addr: 0x%lx, block size: %d\n", i, blockAddr, blockSize);
		XXH64_hash_t h = XXH64(blockAddr, blockSize, 0);

		if (dedup.count(h)) { // deduplication
			dedup_lazy_recipe.push_back({i, dedup[h]});
			continue;
		}

		dedup[h] = i;

		if (network.push(blockAddr, blockSize, i)) {	//if match the batch size of network
			vector<pair<MYHASH, int>> myhash = network.request();
			for (int j = 0; j < myhash.size(); ++j) {
//				cout<<"procssing block: "<<i<<endl;
				RECIPE r;

				//first is hash value, second is chunk id
				MYHASH& h = myhash[j].first;
				int index = myhash[j].second;

				char* chunkAddr = std::get<0>(f.trace[index]);
				int chunkSize = std::get<1>(f.trace[index]);
				int comp_self = LZ4_compress_default(chunkAddr, compressed,
							 chunkSize, 2 * BLOCK_SIZE);
				int dcomp_ann = INF, dcomp_ann_ref;
				dcomp_ann_ref = ann.request(h);

				if (dcomp_ann_ref != -1) {
					dcomp_ann = xdelta3_compress( chunkAddr,
							 chunkSize,
							 std::get<0>(f.trace[dcomp_ann_ref]),
							 std::get<1>(f.trace[dcomp_ann_ref]),
							 delta_compressed, 1);
				}

				set_offset(r, total);

				//choose the minimum type of compress to store
				if (min(comp_self, chunkSize) > dcomp_ann) { // delta compress
					set_size(r, (unsigned long)(dcomp_ann - 1));
					set_ref(r, dcomp_ann_ref);
					set_flag(r, 0b11);
					f.write_file(delta_compressed, dcomp_ann);
					total += dcomp_ann;
					xdeltaSavedSpace += chunkSize - dcomp_ann;
				} else {
					if (comp_self < chunkSize) { // self compress
						set_size(r, (unsigned long)(comp_self - 1));
						set_flag(r, 0b01);
						f.write_file(compressed, comp_self);
						total += comp_self;
					}
					else { // no compress
						set_flag(r, 0b00);
						f.write_file(chunkAddr, chunkSize);
						total += chunkSize;
					}
				}
#ifdef PRINT_HASH
				cout << index << ' ' << h << '\n';
#endif

				ann.insert(h, index);

				//porcess the duplication
				while (!dedup_lazy_recipe.empty() && dedup_lazy_recipe.begin()->first < index) {
					RECIPE rr;
					set_ref(rr, dedup_lazy_recipe.begin()->second);
					set_flag(rr, 0b10);
					f.recipe_insert(rr);
					dedup_lazy_recipe.pop_front();
				}
				f.recipe_insert(r);
			}
		}
	}
	// LAST REQUEST
	{
		vector<pair<MYHASH, int>> myhash = network.request();
		for (int j = 0; j < myhash.size(); ++j) {
			RECIPE r;

			MYHASH& h = myhash[j].first;
			int index = myhash[j].second;

			char* chunkAddr = std::get<0>(f.trace[index]);
			int chunkSize = std::get<1>(f.trace[index]);

			int comp_self = LZ4_compress_default(chunkAddr, compressed, chunkSize, 2 * BLOCK_SIZE);
			int dcomp_ann = INF, dcomp_ann_ref;
			dcomp_ann_ref = ann.request(h);

			if (dcomp_ann_ref != -1) {
				dcomp_ann = xdelta3_compress(chunkAddr,
					 chunkSize,
					 std::get<0>(f.trace[dcomp_ann_ref]),
					 std::get<1>(f.trace[dcomp_ann_ref]),
					 delta_compressed,
					 1);
			}

			set_offset(r, total);

			if (min(comp_self, chunkSize) > dcomp_ann) { // delta compress
				set_size(r, (unsigned long)(dcomp_ann - 1));
				set_ref(r, dcomp_ann_ref);
				set_flag(r, 0b11);
				f.write_file(delta_compressed, dcomp_ann);
				total += dcomp_ann;
			} else {
				if (comp_self < chunkSize) { // self compress
					set_size(r, (unsigned long)(comp_self - 1));
					set_flag(r, 0b01);
					f.write_file(compressed, comp_self);
					total += comp_self;
				} else { // no compress
					set_flag(r, 0b00);
					f.write_file(chunkAddr, chunkSize);
					total += chunkSize;
				}
			}
#ifdef PRINT_HASH
				cout << index << ' ' << h << '\n';
#endif

			ann.insert(h, index);

			while (!dedup_lazy_recipe.empty() && dedup_lazy_recipe.begin()->first < index) {
				RECIPE rr;
				set_ref(rr, dedup_lazy_recipe.begin()->second);
				set_flag(rr, 0b10);
				f.recipe_insert(rr);
				dedup_lazy_recipe.pop_front();
			}
			f.recipe_insert(r);
		}
	}
	f.recipe_write();
	cout << "Total time: " << f.time_check_end() << "us\n";

	printf("ANN %s with model %s\n", argv[1], argv[2]);
	printf("xdelta saved space: %d, (%.2lf%%)\n", xdeltaSavedSpace, (double)xdeltaSavedSpace*100/(inputSize));
	printf("Input size: %lu, final size: %llu (%.2lf%%)\n", inputSize, total, (double)total * 100 / inputSize);
}
