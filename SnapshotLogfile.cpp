#include "SnapshotLogfile.h"

SnapshotLogfile::SnapshotLogfile(string _linkName) : linkName(_linkName),outputStream(NULL),counter(0) {
	unlink(linkName.c_str());
	permFile = linkName+"_perm";
	if ( symlink(permFile.c_str(),linkName.c_str()) != 0 ) {
		fprintf(stderr,"Warning: Unable to link %s to %s. ErrorCode = %d.\n",linkName.c_str(),permFile.c_str(),errno);
	}
}

bool SnapshotLogfile::write(const char* _string) {
	if( outputStream == NULL ) {
		updateFilename();
		outputStream = fopen(snapshotFilename.c_str(),"w");
		if( outputStream == NULL ) { 
			fprintf(stderr,"Warning: Unable to open %s. ErrorCode = %d.\n",snapshotFilename.c_str(),errno);
			return false;
		}
	}

	int res = fputs(_string,outputStream);

	if( res == EOF ) {
		if(outputStream)
			fclose(outputStream);
		return false;
	}
	return true;
}

void SnapshotLogfile::updateFilename() {
	char buf[255];
	sprintf(buf,"%s_%d",linkName.c_str(),counter);
	counter++;
	if( counter > 2 ) counter = 0;
	snapshotFilename = buf;
}

bool SnapshotLogfile::complete() {
	if( outputStream == NULL ) return true;
	fclose(outputStream);
	if ( rename(snapshotFilename.c_str(),permFile.c_str()) != 0 ) {
		fprintf(stderr,"Warning: Unable to rename %s to %s. ErrorCode = %d.\n",snapshotFilename.c_str(),permFile.c_str(),errno);
		return false;
	}
	updateFilename();
	outputStream = fopen(snapshotFilename.c_str(),"w");
	if( outputStream == NULL ) { 
		fprintf(stderr,"Warning: Unable to open %s. ErrorCode = %d.\n",snapshotFilename.c_str(),errno);
		return false;
	}
	return true;
}

SnapshotLogfile::~SnapshotLogfile() {
	if( outputStream != NULL ) fclose(outputStream);
}

