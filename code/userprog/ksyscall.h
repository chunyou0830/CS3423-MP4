/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__ 
#define __USERPROG_KSYSCALL_H__ 

#include "kernel.h"

#include "synchconsole.h"


void SysHalt()
{
  kernel->interrupt->Halt();
}

int SysAdd(int op1, int op2)
{
  return op1 + op2;
}

int SysCreate(char *filename)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename);
}

int SysOpen(char *filename)
{
	// return value
	// return ID
	// return -1 if failed
	OpenFile *file;
	file = kernel->fileSystem->Open(filename);
	int position = -1;
	int i;	
	for(i=0; i<20; i++){
		if(kernel->fileSystem->fileDescriptorTable[i] == NULL){
			position = i;
			break;
		}	
	}
	if(position>=0 && position<20){
		kernel->fileSystem->fileDescriptorTable[i] = file;
		return position+1;
	}
	else{
		return -1;
	}
	
}

int SysWrite(char *buf, int len, int id){
	return kernel->fileSystem->Write(buf,len,id);
}

int SysRead(char *buf, int len, int id){
	return kernel->fileSystem->Read(buf,len,id);
}

int SysClose(int id){
	return kernel->fileSystem->Close(id);
}

#endif /* ! __USERPROG_KSYSCALL_H__ */
