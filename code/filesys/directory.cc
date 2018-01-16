// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"

#define NumDirEntries	64	//MP4 MODIFIED

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
	table = new DirectoryEntry[size];
	
	// MP4 mod tag
	memset(table, 0, sizeof(DirectoryEntry) * size);  // dummy operation to keep valgrind happy
		
	tableSize = size;
	//cout << "Directory inited, with tablesize = " << tableSize << endl;
	for (int i = 0; i < tableSize; i++)
	table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{ 
	delete [] table;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{
	(void) file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
	//cout << "TableSize After Fetched : " << tableSize << endl;
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
	(void) file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name)
{
	//cout << "[DirFindIndex]\tInside" << endl;
	//cout << "Fetched table size = " << tableSize << endl;
	for (int i = 0; i < tableSize; i++){
		//cout << i << endl;
		if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
		return i;
	}
	//cout << "[DirFindIndex]\tCannot found" << endl;
	return -1;		// name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name, bool recursively)
{
	int i = FindIndex(name);
	//cout << "DIRECTORY::FIND i = " << i << endl;
	if (i == -1){
		if(recursively){
			int result = -1;
			for(int j=0; j<tableSize; j++){
				if(table[j].inUse && (table[j].type==DIR)){
					Directory *childDirectory = new Directory(NumDirEntries);
					OpenFile *childDirectoryFile = new OpenFile(table[j].sector);
					childDirectory->FetchFrom(childDirectoryFile);
					result = childDirectory->Find(name, true);
					delete childDirectory;
					delete childDirectoryFile;
				}
				if(result != -1){
					return result;
				}
			}
		}
		return -1;
	}
	else{
		return table[i].sector;
	}
}

//----------------------------------------------------------------------
// Directory::Add
// 	MP4 MODIFIED
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector, int fileType)
{ 
	if (FindIndex(name) != -1)
	return FALSE;

	for (int i = 0; i < tableSize; i++)
		if (!table[i].inUse) {
			table[i].inUse = TRUE;
			strncpy(table[i].name, name, FileNameMaxLen); 
			table[i].sector = newSector;
			table[i].type = fileType;
		return TRUE;
	}
	return FALSE;	// no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name)
{ 
	int i = FindIndex(name);

	if (i == -1)
	return FALSE; 		// name not in directory
	table[i].inUse = FALSE;
	return TRUE;	
}

bool
Directory::RemoveAll(PersistentBitmap* freeMap, OpenFile *op){
	for(int i=0; i<tableSize; i++){
		if(table[i].inUse){
			if(table[i].type == DIR){
				Directory *dir = new Directory(NumDirEntries);
				OpenFile *of = new OpenFile(table[i].sector);
				dir->FetchFrom(of);
				dir->RemoveAll(freeMap,of);
				delete dir;
				delete of;
			}
				FileHeader *filehdr = new FileHeader;
				filehdr->FetchFrom(table[i].sector);
				table[i].inUse = FALSE;
				filehdr->Deallocate(freeMap);
				freeMap->Clear(table[i].sector);
				delete filehdr;
		}
	}
	this->WriteBack(op);
}

//----------------------------------------------------------------------
// Directory::List
//  MP4 MODIFIED.
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

void
Directory::List(int level, bool recursively)
{
	for(int i=0; i<tableSize; i++){
		if(table[i].inUse){
			if(table[i].type == DIR){
				for(int j=0; j<level; j++){
					cout << "\t";
				}
				cout << "[" << i << "] " << table[i].name << " D" << endl;
				if(recursively){
					Directory *childDirectory = new Directory(NumDirEntries);
					OpenFile *childDirectoryFile = new OpenFile(table[i].sector);
					childDirectory->FetchFrom(childDirectoryFile);
					childDirectory->List(level+1, recursively);
					delete childDirectory;
					delete childDirectoryFile;
				}
			}
			else if(table[i].type == FILE){
				for(int j=0; j<level; j++){
					cout << "\t";
				}
				cout << "[" << i << "] " << table[i].name << " F" << endl;
			}
		}
	}
	
	/*for (int i = 0; i < tableSize; i++)
		if (table[i].inUse)
			printf("%s\n", table[i].name);*/
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print()
{ 
	FileHeader *hdr = new FileHeader;

	//cout << "Direcotry tableSize: " << tableSize << endl;
	printf("Directory contents:\n");
	for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
		printf("Name: %s, Sector: %d\n", table[i].name, table[i].sector);
		hdr->FetchFrom(table[i].sector);
		hdr->Print();
	}
	printf("\n");
	delete hdr;
}
