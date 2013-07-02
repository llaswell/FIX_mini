#ifndef SPLIT_H
#define SPLIT_H

#include <string>
#include <algorithm>
#include <vector>

using namespace std;

void split(const string& str,
		vector<string>& tokens,
		const string& delimiters = " ");

#endif //SPLIT_H
