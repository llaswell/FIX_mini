#include "split.h"

#include <iostream>

void split(const string& str,
		vector<string>& tokens,
		const string& delimiters) {

	string::size_type lastPos = 0;

	string::size_type pos = str.find_first_of(delimiters, lastPos);

	while( !((string::npos == pos) || (string::npos == lastPos)) ) {
		tokens.push_back(str.substr(lastPos,pos - lastPos));
		lastPos = ++pos;
		pos = str.find_first_of(delimiters, lastPos);
	}
	tokens.push_back(str.substr(lastPos,pos - lastPos));
}
