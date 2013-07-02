#ifndef SNAPSHOTLOGFILE_H
#define SNAPSHOTLOGFILE_H

#include <string>
#include <cstdio>
#include <errno.h>
#include <iostream>

using namespace std;

class SnapshotLogfile {
public:
	SnapshotLogfile(string _linkName);
	bool write(const char* _string);
	bool complete();
	~SnapshotLogfile();

private:

	void updateFilename();
	FILE* outputStream;
	string linkName;
	string snapshotFilename;

	int counter;
	string permFile;
};
#endif //#define SNAPSHOTLOGFILE_H
