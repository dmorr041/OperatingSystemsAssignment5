#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "LibDisk.h"
#include "LibFS.h"
#include <ctype.h>
#include <stdbool.h>

// Used for de-bug information
#define FSDEBUG 1

#if FSDEBUG
#define dprintf printf
#else
#define dprintf noprintf
void noprintf(char* str, ...) {}
#endif

// the file system partitions the disk into five parts:

// 1. the superblock (one sector), which contains a magic number at
// its first four bytes (integer)
#define SUPERBLOCK_START_SECTOR 0

// the magic number chosen for our file system
#define OS_MAGIC 0xdeadbeef

// 2. the inode bitmap (one or more sectors), which indicates whether
// the particular entry in the inode table (#4) is currently in use
#define INODE_BITMAP_START_SECTOR 1

// the total number of bytes and sectors needed for the inode bitmap;
// we use one bit for each inode (whether it's a file or directory) to
// indicate whether the particular inode in the inode table is in use
#define INODE_BITMAP_SIZE ((MAX_FILES+7)/8)                                           
#define INODE_BITMAP_SECTORS ((INODE_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)         

// 3. the sector bitmap (one or more sectors), which indicates whether
// the particular sector in the disk is currently in use
#define SECTOR_BITMAP_START_SECTOR (INODE_BITMAP_START_SECTOR+INODE_BITMAP_SECTORS)   

// the total number of bytes and sectors needed for the data block
// bitmap (we call it the sector bitmap); we use one bit for each
// sector of the disk to indicate whether the sector is in use or not
#define SECTOR_BITMAP_SIZE ((TOTAL_SECTORS+7)/8)                                      
#define SECTOR_BITMAP_SECTORS ((SECTOR_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)        

// 4. the inode table (one or more sectors), which contains the inodes
// stored consecutively
#define INODE_TABLE_START_SECTOR (SECTOR_BITMAP_START_SECTOR+SECTOR_BITMAP_SECTORS)  

// an inode is used to represent each file or directory; the data
// structure supposedly contains all necessary information about the
// corresponding file or directory
typedef struct _inode {
  int size; // the size of the file or number of directory entries
  int type; // 0 means regular file; 1 means directory
  int data[MAX_SECTORS_PER_FILE]; // indices to sectors containing data blocks
} inode_t;

// the inode structures are stored consecutively and yet they don't
// straddle accross the sector boundaries; that is, there may be
// fragmentation towards the end of each sector used by the inode
// table; each entry of the inode table is an inode structure; there
// are as many entries in the table as the number of files allowed in
// the system; the inode bitmap (#2) indicates whether the entries are
// current in use or not
#define INODES_PER_SECTOR (SECTOR_SIZE/sizeof(inode_t))                             
#define INODE_TABLE_SECTORS ((MAX_FILES+INODES_PER_SECTOR-1)/INODES_PER_SECTOR)     

// 5. the data blocks; all the rest sectors are reserved for data
// blocks for the content of files and directories
#define DATABLOCK_START_SECTOR (INODE_TABLE_START_SECTOR+INODE_TABLE_SECTORS)       

// other file related definitions

// max length of a path is 256 bytes (including the ending null)
#define MAX_PATH 256

// max length of a filename is 16 bytes (including the ending null)
#define MAX_NAME 16

// max number of open files is 256
#define MAX_OPEN_FILES 256

// each directory entry represents a file/directory in the parent
// directory, and consists of a file/directory name (less than 16
// bytes) and an integer inode number
typedef struct _dirent {
  char fname[MAX_NAME]; // name of the file
  int inode; // inode of the file
} dirent_t;

// the number of directory entries that can be contained in a sector
#define DIRENTS_PER_SECTOR (SECTOR_SIZE/sizeof(dirent_t))               

// global errno value here
int osErrno;

// the name of the disk backstore file (with which the file system is booted)
static char bs_filename[1024];




/********************** HELPER FUNCTIONS ******************************/

// check magic number in the superblock; return 1 if OK, and 0 if not
static int check_magic()
{
  char buffer[SECTOR_SIZE];
  if(Disk_Read(SUPERBLOCK_START_SECTOR, buffer) < 0)
    return 0;
  if(*(int*)buffer == OS_MAGIC) return 1;
  else return 0;
}



// Function to set the nth bit of the bitmap
static char set_NthBit (unsigned char c, int n) 
{
    static unsigned char mask[] = {128, 64, 32, 16, 8, 4, 2, 1};
    return (c | mask[n]);
}



// Function to determine if the Nth bit is set in bitmap (1 if yes, 0 if no)
static int isNthSet (unsigned char c, int n) 
{
    static unsigned char mask[] = {128, 64, 32, 16, 8, 4, 2, 1};
    return ((c & mask[n]) != 0);
}

/************************** END OF HELPER FUNCTIONS *************************************************/

/************************** BITMAP FUNCTIONS *********************************************************/


// initialize a bitmap with 'num' sectors starting from 'start'
// sector; all bits should be set to zero except that the first
// 'nbits' number of bits are set to one
static void bitmap_init(int start, int num, int nbits)
{
  
	int nBytes = nbits / 8;        	// Number of bytes to set to 1  
	int remain_bits = nbits % 8; 	// Remainder bits 
	int last_block = 0;           	// Boolean integer indicating whether or not we are on the last block to set 1s on
	char buffer[SECTOR_SIZE];		// Sector buffer
  
	int i;
  
	for(i = start; i < (start + num); i++)
	{     
		if(nBytes < SECTOR_SIZE)
			last_block = 1;            
		

		// Read the disk
		if(Disk_Read(i, buffer) < 0)
		{                                    
			dprintf("Failed to read block %d\n" , i);
			osErrno = E_GENERAL;
			return;  
		}    
   
		memset(buffer, 0, SECTOR_SIZE);     // Set to all zeroes

		// If we have at least one byte to set 1s in
		if(nBytes >= 0)
		{               
			if(last_block == 0)
			{             
				memset(buffer, 1, SECTOR_SIZE);    	// Set to all 1s
				nBytes = nBytes - SECTOR_SIZE;  	// Update the remaining bytes to set 1s in
			}
			else
			{
				memset(buffer, 255, nBytes);         // Set only nByte bytes

				//If there is remaining bits to set to 1 and this is the last block to set 1s in
				if(remain_bits > 0 && last_block == 1)
				{               
					unsigned char mask = (255<<(8 - remain_bits));    
					 
					buffer[nBytes] = mask;                     
					nBytes = -1;
				}
			}
		}      

		// Write the disk
		if(Disk_Write(i, buffer) < 0) 
		{                                     
			dprintf("Failed to write block %d\n" , i);
			osErrno = E_GENERAL;
			return;
		}
	}
}





// set the first unused bit from a bitmap of 'nbits' bits (flip the
// first zero appeared in the bitmap to one) and return its location;
// return -1 if the bitmap is already full (no more zeros)
static int bitmap_first_unused(int start, int num, int nbits)
{
	int nBytes = nbits / 8;                 // Number of bytes to set to 1  
	int remain_bits = nbits % 8;            // Remainder bits to set to 1
	int last_block = 0;                    	// Boolean integer indicating the last block to set 1s in
	char buffer[SECTOR_SIZE];               
	int location = -1;                      // Return location 
  
	int i;
	
	for(i = start; i < (start + num); i++)
	{     

		// If number of bytes to set < sector size
		if(nBytes < SECTOR_SIZE)
			last_block = 1;      	// This is the last block to set 1s in      

		// Read the sector
		if(Disk_Read(i, buffer) < 0)
		{                                      
			dprintf("Failed to read block %d\n" , i);
			osErrno = E_GENERAL;
			return -1;  
		}

		int x;       
		int bitInByte; 
		int last; 

		// If this is the last sector
		if(last_block == 1)                 
			last = nBytes;		// Stop at nBytes
		else                                 
			last = SECTOR_SIZE; 	// Stop at end of sector (which means we have more sectors to set)
   
        
		// Loop through all compeleted bytes 
		for(x = 0; x < last; x++)
		{       
			// Loop through the bits in each byte
			for(bitInByte = 0; bitInByte < 8; bitInByte++)
			{  
				location ++;
			
				// Check if this bit is set
				if(!isNthSet(buffer[x], bitInByte))
				{                 
					buffer[x] = set_NthBit(buffer[x], bitInByte);          //Set this bit
           
					// Write sector back 
					if(Disk_Write(i, buffer) < 0) 
					{              
						dprintf("Failed to write block %d\n" , i);
						osErrno = E_GENERAL;
						return -1;
					}
			
					return location;        // Return the first unused spot
				}
        
			}     
		} 

		// Need to check remaining bits in byte
		if(last_block == 1)
		{   
			// Loop through the remaining bits in the byte
			for(bitInByte = 0; bitInByte < remain_bits ; bitInByte++)
			{  
				location ++;
			
				if(!isNthSet(buffer[x], bitInByte))
				{        //Chek if this bit is not set
					buffer[x] = set_NthBit(buffer[x], bitInByte);          //Set this bit

					// Write sector back
					if(Disk_Write(i, buffer) < 0) 
					{              
						dprintf("Failed to write block %d\n" , i);
						osErrno = E_GENERAL;
						return -1;
					}
				
					return location;        // Return the first unused spot
				}
			} 
		}
	}

	return -1;
}






// reset the i-th bit of a bitmap with 'num' sectors starting from
// 'start' sector; return 0 if successful, -1 otherwise
static int bitmap_reset(int start, int num, int ibit)
{
  
	int nBytes = ibit / 8;                     	// Completed bytes up to the one we're going to set a bit in
	int remain_bits = ibit % 8;                 // Position of the bit to reset
	char buffer[SECTOR_SIZE];                  
          
    if(nBytes > SECTOR_SIZE * num)
	{
		dprintf("Error: ibit value of %d is too big for a sector\n" , ibit); 
		return -1;                 
    }

	// Loop to the last block 
    while(nBytes > SECTOR_SIZE)
	{
		nBytes = nBytes - SECTOR_SIZE;   
    }

    if(Disk_Read(start, buffer) < 0)
	{                                      
		dprintf("Failed to read block %d\n" , start);
		osErrno = E_GENERAL;
		return -1;  
    }

    static unsigned char mask[] = {127, 191, 223, 239, 247, 251, 253, 254};
	
    buffer[nBytes] = (buffer[nBytes] & mask[remain_bits]);

    if(Disk_Write(start, buffer) < 0) 
	{                                        
        dprintf("Failed to write block %d\n" , start);
        osErrno = E_GENERAL;
		return -1;
	}
    
	return 0;
}


/************************** END OF BITMAP FUNCTIONS *********************************************************/



// return 1 if the file name is illegal; otherwise, return 0; legal
// characters for a file name include letters (case sensitive),
// numbers, dots, dashes, and underscores; and a legal file name
// should not be more than MAX_NAME-1 in length
static int illegal_filename(char* name)
{
	int i;	
	bool flag = true;	
	
	// Loop through the string; If a character is not alphanumeric, a dot, a dash, or an underscore, 
	// the filename is illegal
	for(i = 0; i < strlen(name) && flag == true; i++)
	{
		if(isalnum(name[i]) || name[i] == '.' || name[i] == '-' || name[i] == '_')
			flag = true;
		else
			flag = false;
	}
	
	// Check if the string is too long
	if(strlen(name) > (MAX_NAME-1))
	{
		flag = false;
	}
	
	// Return 0 for legal filenames
	if(flag)
		return 0;

	// Return 1 for illegal filenames
	return 1; 
}
  


// return the child inode of the given file name 'fname' from the
// parent inode; the parent inode is currently stored in the segment
// of inode table in the cache (we cache only one disk sector for
// this); once found, both cached_inode_sector and cached_inode_buffer
// may be updated to point to the segment of inode table containing
// the child inode; the function returns -1 if no such file is found;
// it returns -2 is something else is wrong (such as parent is not
// directory, or there's read error, etc.)
static int find_child_inode(int parent_inode, char* fname, int *cached_inode_sector, char* cached_inode_buffer){

  int cached_start_entry = ((*cached_inode_sector)-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  int offset = parent_inode-cached_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* parent = (inode_t*)(cached_inode_buffer+offset*sizeof(inode_t));
  dprintf("... load parent inode: %d (size=%d, type=%d)\n",	parent_inode, parent->size, parent->type);
  if(parent->type != 1) {
    dprintf("... parent not a directory\n");
    return -2;
  }

  int nentries = parent->size; // remaining number of directory entries 
  int idx = 0;
  while(nentries > 0) {
    char buffer[SECTOR_SIZE]; // cached content of directory entries
    if(Disk_Read(parent->data[idx], buffer) < 0) return -2;
    int i;
    for(i=0; i<DIRENTS_PER_SECTOR; i++) {
      if(i>nentries) break;
      if(!strcmp(((dirent_t*)buffer)[i].fname, fname)) {
	       // found the file/directory; update inode cache
	       int child_inode = ((dirent_t*)buffer)[i].inode;
	       dprintf("... found child_inode=%d\n", child_inode);
	       int sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
	       if(sector != (*cached_inode_sector)) {
	         *cached_inode_sector = sector;
	         if(Disk_Read(sector, cached_inode_buffer) < 0) return -2;
	           dprintf("... load inode table for child\n");
	       }
	       return child_inode;
      }
    }
    idx++; nentries -= DIRENTS_PER_SECTOR;
  }
  dprintf("... could not find child inode\n");
  return -1; // not found
}

// follow the absolute path; if successful, return the inode of the
// parent directory immediately before the last file/directory in the
// path; for example, for '/a/b/c/d.txt', the parent is '/a/b/c' and
// the child is 'd.txt'; the child's inode is returned through the
// parameter 'last_inode' and its file name is returned through the
// parameter 'last_filename' (both are references); it's possible that
// the last file/directory is not in its parent directory, in which
// case, 'last_inode' points to -1; if the function returns -1, it
// means that we cannot follow the path
static int follow_path(char* path, int* last_inode, char* last_filename)
{
  if(!path) {
    dprintf("... invalid path\n");
    return -1;
  }
  if(path[0] != '/') {
    dprintf("... '%s' not absolute path\n", path);
    return -1;
  }
  
  // make a copy of the path (skip leading '/'); this is necessary
  // since the path is going to be modified by strsep()
  char pathstore[MAX_PATH]; 
  strncpy(pathstore, path+1, MAX_PATH-1);
  pathstore[MAX_PATH-1] = '\0'; // for safety
  char* lpath = pathstore;
  
  int parent_inode = -1, child_inode = 0; // start from root
  // cache the disk sector containing the root inode
  int cached_sector = INODE_TABLE_START_SECTOR;
  char cached_buffer[SECTOR_SIZE];
  if(Disk_Read(cached_sector, cached_buffer) < 0) return -1;
  dprintf("... load inode table for root from disk sector %d\n", cached_sector);
  
  // for each file/directory name separated by '/'
  char* token;
  while((token = strsep(&lpath, "/")) != NULL) {
    dprintf("... process token: '%s'\n", token);
    if(*token == '\0') continue; // multiple '/' ignored
    if(illegal_filename(token)) {
      dprintf("... illegal file name: '%s'\n", token);
      return -1; 
    }
    if(child_inode < 0) {
      // regardless whether child_inode was not found previously, or
      // there was issues related to the parent (say, not a
      // directory), or there was a read error, we abort
      dprintf("... parent inode can't be established\n");
      return -1;
    }
    parent_inode = child_inode;    
    child_inode = find_child_inode(parent_inode, token, &cached_sector, cached_buffer);    

    if(last_filename) strcpy(last_filename, token);
  }
  if(child_inode < -1) return -1; // if there was error, abort
  else {
    // there was no error, several possibilities:
    // 1) '/': parent = -1, child = 0
    // 2) '/valid-dirs.../last-valid-dir/not-found': parent=last-valid-dir, child=-1
    // 3) '/valid-dirs.../last-valid-dir/found: parent=last-valid-dir, child=found
    // in the first case, we set parent=child=0 as special case
    if(parent_inode==-1 && child_inode==0) parent_inode = 0;
    dprintf("... found parent_inode=%d, child_inode=%d\n", parent_inode, child_inode);
    *last_inode = child_inode;
    return parent_inode;
  }
}

// add a new file or directory (determined by 'type') of given name
// 'file' under parent directory represented by 'parent_inode'
int add_inode(int type, int parent_inode, char* file)
{
  // get a new inode for child
  int child_inode = bitmap_first_unused(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, INODE_BITMAP_SIZE);
  
  if(child_inode < 0) {
    dprintf("... error: inode table is full\n");
    return -1; 
  }
  dprintf("... new child inode %d\n", child_inode);

  // load the disk sector containing the child inode
  int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
 // printf("Inode sector = %d\n", inode_sector);

  char inode_buffer[SECTOR_SIZE];
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);

  // get the child inode
  int inode_start_index = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
 
  int offset = child_inode-inode_start_index;
  
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));

  // update the new child inode and write to disk
  memset(child, 0, sizeof(inode_t));
  child->type = type;
  if(Disk_Write(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... update child inode %d (size=%d, type=%d), update disk sector %d\n", child_inode, child->size, child->type, inode_sector);

  // get the disk sector containing the parent inode
  inode_sector = INODE_TABLE_START_SECTOR+parent_inode/INODES_PER_SECTOR;
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for parent inode %d from disk sector %d\n", parent_inode, inode_sector);

  // get the parent inode
  inode_start_index = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  offset = parent_inode-inode_start_index;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* parent = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
  dprintf("... get parent inode %d (size=%d, type=%d)\n", parent_inode, parent->size, parent->type);

  // get the dirent sector
  if(parent->type != 1) {
    dprintf("... error: parent inode is not directory\n");
    return -2; // parent not directory
  }
  int group = parent->size/DIRENTS_PER_SECTOR;
  char dirent_buffer[SECTOR_SIZE];
  if(group*DIRENTS_PER_SECTOR == parent->size) {
    // new disk sector is needed
    int newsec = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);
    if(newsec < 0) {
      dprintf("... error: disk is full\n");
      return -1;
    }
    parent->data[group] = newsec;
    memset(dirent_buffer, 0, SECTOR_SIZE);
    dprintf("... new disk sector %d for dirent group %d\n", newsec, group);
  } else {
    if(Disk_Read(parent->data[group], dirent_buffer) < 0)
      return -1;
    dprintf("... load disk sector %d for dirent group %d\n", parent->data[group], group);
  }

  // add the dirent and write to disk
  int start_entry = group*DIRENTS_PER_SECTOR;
  offset = parent->size-start_entry;
  dirent_t* dirent = (dirent_t*)(dirent_buffer+offset*sizeof(dirent_t));
  strncpy(dirent->fname, file, MAX_NAME);
  dirent->inode = child_inode;
  if(Disk_Write(parent->data[group], dirent_buffer) < 0) return -1;
  dprintf("... append dirent %d (name='%s', inode=%d) to group %d, update disk sector %d\n", parent->size, dirent->fname, dirent->inode, group, parent->data[group]);

  // update parent inode and write to disk
  parent->size++;
  if(Disk_Write(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... update parent inode on disk sector %d\n", inode_sector);
    
  return 0;
}

// used by both File_Create() and Dir_Create(); type=0 is file, type=1
// is directory
int create_file_or_directory(int type, char* pathname)
{
  int child_inode;
  char last_filename[MAX_NAME];
  int parent_inode = follow_path(pathname, &child_inode, last_filename);
  if(parent_inode >= 0) {
    if(child_inode >= 0) {
      dprintf("... file/directory '%s' already exists, failed to create\n", pathname);
      osErrno = E_CREATE;
      return -1;
    } else {
        if(add_inode(type, parent_inode, last_filename) >= 0) {
  	      dprintf("... successfully created file/directory: '%s'\n", pathname);
  	      return 0;
        } else {
  	      dprintf("... error: something wrong with adding child inode\n");
  	      osErrno = E_CREATE;
  	      return -1;
        }
      }
  } else {
    dprintf("... error: something wrong with the file/path: '%s'\n", pathname);
    osErrno = E_CREATE;
    return -1;
  }
}





// remove the child from parent; the function is called by both
// File_Unlink() and Dir_Unlink(); the function returns 0 if success,
// -1 if general error, -2 if directory not empty, -3 if wrong type
int remove_inode(int type, int parent_inode, int child_inode)
{
	// First, load child inode sector
	int inode_sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
 
	char inode_buffer[SECTOR_SIZE];
  
	if(Disk_Read(inode_sector, inode_buffer) < 0) 
		return -1;
  
	dprintf("Load inode table for child inode from disk sector %d\n", inode_sector);

	// Get child inode
	int inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
 
	int offset = child_inode - inode_start_index;
  
	assert(0 <= offset && offset < INODES_PER_SECTOR);
	inode_t* child = (inode_t*)(inode_buffer + offset * sizeof(inode_t));

	// If we have the wrong type
	if(child->type != type)
		return -3;               

	// If node is a nonempty directory
	if(child->type == 1 && child->size > 0)
		return -2;                               

	// If node is a file, reclaim data sectors of child inode
	if(child->type == 0)
	{
		int i;
		
		// Loop through all sectors
		for(i = 0; i < MAX_SECTORS_PER_FILE; i++)
		{   
			// If we have valid data we need to clear
			if(child->data[i] > 0)
			{           
				bitmap_reset(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, child->data[i]);    // Reset the entry in the sector bitmap
				dprintf("Resetting bit sector %d from index [%d]\n", child->data[i], i );
			}
		}
	}
  
	// Clear the child
	memset(child, 0, sizeof(inode_t));
  
	// Write to disk
	if(Disk_Write(inode_sector, inode_buffer) < 0) 
		return -1;
	
	dprintf("Update disk sector %d\n", inode_sector);

	// Update inode bitmap
	bitmap_reset(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, child_inode);

	// Get sector containing parent 
	inode_sector = INODE_TABLE_START_SECTOR + parent_inode / INODES_PER_SECTOR;
	
	// Read the disk
	if(Disk_Read(inode_sector, inode_buffer) < 0) 
		return -1;
	
	dprintf("Load inode table for parent inode %d from disk sector %d\n", parent_inode, inode_sector);

	// Get the parent
	inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
	offset = parent_inode - inode_start_index;
	
	assert(0 <= offset && offset < INODES_PER_SECTOR);
	
	inode_t* parent = (inode_t*)(inode_buffer + offset * sizeof(inode_t));
	dprintf("Get parent: %d, size: %d, type: %d\n", parent_inode, parent->size, parent->type);

	
	// If a parent is somehow not a parent
	if(parent->type != 1) 
	{
		dprintf("ERROR: parent inode is not a directory\n");
		return -2; 
	}
  
	// Now we find (in parent inode) the dirent structure containing child inode, swap with last dirent entry in parent
	char dirent_buffer[SECTOR_SIZE];
	
	int group = 0;
    int entry = 0;  
	
    dirent_t* current_dirent;

	if(parent->size > 1)
	{  
		int last_group = parent->size / DIRENTS_PER_SECTOR;
		char last_dirent_buffer[SECTOR_SIZE];
		int last_sector = parent->data[last_group];
		
		if(Disk_Read(last_sector, last_dirent_buffer) < 0)  
			return -1;
		
		dprintf("Load sector %d corresponding to last dirent in group %d\n", parent->data[last_group], group);

		// Get the last dirent entry
		int start_entry = last_group * DIRENTS_PER_SECTOR;
		offset = parent->size - start_entry - 1;
		
		// Last dirent to swap with the one we're deleting
		dirent_t* last_dirent = (dirent_t*)(last_dirent_buffer + offset * sizeof(dirent_t));  
    
		// Loop through all groups    
		for(group = 0; group < MAX_SECTORS_PER_FILE; group++)
		{       
			// Read sector in this group
			if(Disk_Read(parent->data[group], dirent_buffer) < 0)    
				return -1;
			
			dprintf("Load disk sector %d for dirent group %d\n", parent->data[last_group], group);
			
			// Loop through all dirents in this group
			for(entry = 0; entry < DIRENTS_PER_SECTOR; entry++)
			{      
				current_dirent = (dirent_t*)(dirent_buffer + entry * sizeof(dirent_t));
				
				if(current_dirent->inode == child_inode)
				{
					strncpy(current_dirent->fname, last_dirent->fname, MAX_NAME);
					current_dirent->inode = last_dirent->inode; 
	
					char * empty = "";
				
					strncpy(last_dirent->fname, empty, MAX_NAME);
					last_dirent->inode = -1;
                     
					memset(last_dirent, 0 , sizeof(dirent_t));
				
					if(Disk_Write(parent->data[group], dirent_buffer) < 0) 
						return -1;     
				
					dprintf("Update dirent %d, name: %s, inode: %d to group %d; update disk sector %d\n", (group * 30) + entry, current_dirent->fname, 
						current_dirent->inode, group, parent->data[group]);
						
					if(Disk_Write(inode_sector, inode_buffer) < 0) 
						return -1;     
				
					group = MAX_SECTORS_PER_FILE; 
				
					break;
				}
			}
		}
	}

	parent->size--;
	
	if(Disk_Write(inode_sector, inode_buffer) < 0) 
		return -1;
  
	dprintf("Update parent inode on disk sector %d\n", inode_sector);
 
	return 0;  
}









// representing an open file
typedef struct _open_file {
  int inode; // pointing to the inode of the file (0 means entry not used)
  int size;  // file size cached here for convenience
  int pos;   // read/write position
} open_file_t;
static open_file_t open_files[MAX_OPEN_FILES];

// return true if the file pointed to by inode has already been open
int is_file_open(int inode)
{
  int i;
  for(i=0; i<MAX_OPEN_FILES; i++) {
    if(open_files[i].inode == inode)
      return 1;
  }
  return 0;
}

// return a new file descriptor not used; -1 if full
int new_file_fd()
{
  int i;
  for(i=0; i<MAX_OPEN_FILES; i++) {
    if(open_files[i].inode <= 0)
      return i;
  }
  return -1;
}

/* end of internal helper functions, start of API functions */





int FS_Boot(char* backstore_fname)
{
  dprintf("FS_Boot('%s'):\n", backstore_fname);
  // initialize a new disk (this is a simulated disk)
  if(Disk_Init() < 0) {
    dprintf("... disk init failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  dprintf("... disk initialized\n");
  
  // we should copy the filename down; if not, the user may change the
  // content pointed to by 'backstore_fname' after calling this function
  strncpy(bs_filename, backstore_fname, 1024);
  bs_filename[1023] = '\0'; // for safety
  
  // we first try to load disk from this file
  if(Disk_Load(bs_filename) < 0) {
    dprintf("... load disk from file '%s' failed\n", bs_filename);

    // if we can't open the file; it means the file does not exist, we
    // need to create a new file system on disk
    if(diskErrno == E_OPENING_FILE) {
      dprintf("... couldn't open file, create new file system\n");

      // format superblock
      char buffer[SECTOR_SIZE];
      memset(buffer, 0, SECTOR_SIZE);
      *(int*)buffer = OS_MAGIC;
      if(Disk_Write(SUPERBLOCK_START_SECTOR, buffer) < 0) {
	    dprintf("... failed to format superblock\n");
	    osErrno = E_GENERAL;
	    return -1;
      }

      dprintf("... formatted superblock (sector %d)\n", SUPERBLOCK_START_SECTOR);

      // format inode bitmap (reserve the first inode to root)
      bitmap_init(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, 1);
      dprintf("... formatted inode bitmap (start=%d, num=%d)\n", (int)INODE_BITMAP_START_SECTOR, (int)INODE_BITMAP_SECTORS);
      
      // format sector bitmap (reserve the first few sectors to
      // superblock, inode bitmap, sector bitmap, and inode table)
      bitmap_init(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS,DATABLOCK_START_SECTOR);
      dprintf("... formatted sector bitmap (start=%d, num=%d)\n",(int)SECTOR_BITMAP_START_SECTOR, (int)SECTOR_BITMAP_SECTORS);
      
      // format inode tables
      int i;
      for(i=0; i<INODE_TABLE_SECTORS; i++) {
	       memset(buffer, 0, SECTOR_SIZE);
	       if(i==0) {
	       // the first inode table entry is the root directory
	         ((inode_t*)buffer)->size = 0;
	         ((inode_t*)buffer)->type = 1;
	       }
	       if(Disk_Write(INODE_TABLE_START_SECTOR+i, buffer) < 0) {
  	        dprintf("... failed to format inode table\n");
  	        osErrno = E_GENERAL;
  	        return -1;	
          } 
        }

      dprintf("... formatted inode table (start=%d, num=%d)\n",(int)INODE_TABLE_START_SECTOR, (int)INODE_TABLE_SECTORS);
      
      // we need to synchronize the disk to the backstore file (so that we don't lose the formatted disk)
      if(Disk_Save(bs_filename) < 0) {
	     // if can't write to file, something's wrong with the backstore
      	dprintf("... failed to save disk to file '%s'\n", bs_filename);
      	osErrno = E_GENERAL;
      	return -1;
      } else {
      	// everything's good now, boot is successful
      	dprintf("... successfully formatted disk, boot successful\n");
      	memset(open_files, 0, MAX_OPEN_FILES*sizeof(open_file_t));
      	return 0;
      }
    } else {
      // something wrong loading the file: invalid param or error reading
      dprintf("... couldn't read file '%s', boot failed\n", bs_filename);
      osErrno = E_GENERAL; 
      return -1;
    }
  }else {
      dprintf("... load disk from file '%s' successful\n", bs_filename);
    
      // we successfully loaded the disk, we need to do two more checks,
      // first the file size must be exactly the size as expected (this
      // supposedly should be folded in Disk_Load(); and it's not)
      int sz = 0;
      FILE* f = fopen(bs_filename, "r");
      if(f) {
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fclose(f);
      }
      if(sz != SECTOR_SIZE*TOTAL_SECTORS) {
        dprintf("... check size of file '%s' failed\n", bs_filename);
        osErrno = E_GENERAL;
        return -1;
      }
      dprintf("... check size of file '%s' successful\n", bs_filename);
    
      // check magic
      if(check_magic()) {
        // everything's good by now, boot is successful
        dprintf("... check magic successful\n");
        memset(open_files, 0, MAX_OPEN_FILES*sizeof(open_file_t));
        return 0;
      } else {      
        // mismatched magic number
        dprintf("... check magic failed, boot failed\n");
        osErrno = E_GENERAL;
        return -1;
      }
  }
}






int FS_Sync()
{
  if(Disk_Save(bs_filename) < 0) {
    // if can't write to file, something's wrong with the backstore
    dprintf("FS_Sync():\n... failed to save disk to file '%s'\n", bs_filename);
    osErrno = E_GENERAL;
    return -1;
  } else {
    // everything's good now, sync is successful
    dprintf("FS_Sync():\n... successfully saved disk to file '%s'\n", bs_filename);
    return 0;
  }  
}






int File_Create(char* file)
{
  dprintf("File_Create('%s'):\n", file);
  return create_file_or_directory(0, file);
}






int File_Unlink(char* file)
{
	dprintf("File_Unlink(%s):\n", file);
	
	int child_inode;
	char last_filename[MAX_NAME];
	
	// Get father inode
	int parent_inode = follow_path(file, &child_inode, last_filename);   
  
	// If father found
	if(parent_inode >= 0) 
	{         
		// If child found
		if(child_inode >= 0) 
		{     
			// If file is already open
			if(is_file_open(child_inode) == 1)
			{     
				osErrno = E_FILE_IN_USE;    
				return -1;    
			}
      
			int result;
			result = remove_inode(0, parent_inode, child_inode); 
      
			switch(result)
			{
				case 0:   dprintf("Succefully removed the inode representing a file\n");
						return 0;

				case -1:  dprintf("General error removing inode\n");
						osErrno = E_GENERAL;
						return -1;

				case -2:  dprintf("Current directory not empty\n");
						osErrno = E_DIR_NOT_EMPTY;
						return -1;

				case -3:  dprintf("Wrong type\n");
						osErrno = E_GENERAL;
						return -1;
			}           

		}
		else
		{
			dprintf("File %s does not exist\n", file);
			osErrno = E_NO_SUCH_FILE;
			return -1;
		}
	}
	else 
	{
		dprintf("ERROR: File/Path invalid: %s\n", file);
		osErrno = E_NO_SUCH_FILE;
		return -1;
	}
  
	return -1;
}









int File_Open(char* file)
{
  dprintf("File_Open('%s'):\n", file);
  int fd = new_file_fd();
  if(fd < 0) {
    dprintf("... max open files reached\n");
    osErrno = E_TOO_MANY_OPEN_FILES;
    return -1;
  }

  int child_inode;
  follow_path(file, &child_inode, NULL);
  if(child_inode >= 0) { // child is the one
    // load the disk sector containing the inode
    int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if(Disk_Read(inode_sector, inode_buffer) < 0) { osErrno = E_GENERAL; return -1; }
    dprintf("... load inode table for inode from disk sector %d\n", inode_sector);

    // get the inode
    int inode_start_index = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
    int offset = child_inode-inode_start_index;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
    dprintf("... inode %d (size=%d, type=%d)\n",
	    child_inode, child->size, child->type);

    if(child->type != 0) {
      dprintf("... error: '%s' is not a file\n", file);
      osErrno = E_GENERAL;
      return -1;
    }

    // initialize open file entry and return its index
    open_files[fd].inode = child_inode;
    open_files[fd].size = child->size;
    open_files[fd].pos = 0;
    return fd;
  } else {
    dprintf("... file '%s' is not found\n", file);
    osErrno = E_NO_SUCH_FILE;
    return -1;
  }  
}








int File_Read(int fd, void* buffer, int size)
{
  
	dprintf("Reading file... \n");
	
	int i;
 
	// Check if file open
	if(is_file_open(open_files[fd].inode) != 1)
	{		
        osErrno = E_BAD_FD;
        return -1; 
    }

	dprintf("open_files.nodes = %d and size %d  and initial position %d \n", open_files[fd].inode, open_files[fd].size, open_files[fd].pos );
	
	// Position at which read ends
	int end_of_read = (open_files[fd].pos + size);
	
	// If reading past the file size, read until EOF
	if(end_of_read > open_files[fd].size)
	{ 
		end_of_read = (open_files[fd].size);
	}

  	// Get child inode
	int child_inode = open_files[fd].inode;		
	int inode_sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR; 
	char inode_buffer[SECTOR_SIZE];
	
	if(Disk_Read(inode_sector, inode_buffer) < 0) 
		return -1;
		
	dprintf("Load inode table for child inode from disk sector %d\n", inode_sector);

	// Get the position we start reading data from 
	int inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
	int offset = child_inode - inode_start_index;
	
	assert(0 <= offset && offset < INODES_PER_SECTOR);
	
	inode_t* child = (inode_t*)(inode_buffer + offset * sizeof(inode_t));

	// If child is not a file
	if(child->type != 0) 
	{
		dprintf("ERROR: inode not a file\n");
		osErrno = E_GENERAL;
		return -1;
	}

	dprintf("Reading inode: %d, size: %d, type: %d\n", child_inode, child->size, child->type);	
 
	int bytes_to_read = end_of_read - open_files[fd].pos;       // Number of bytes we have to read
	int bytes_read = bytes_to_read;   							// Number of bytes that have been read                                 

	int offset_sector_toLoad = open_files[fd].pos / SECTOR_SIZE;  // Starting index of data sectors to be loaded for the read 
	int sector_index = open_files[fd].pos % SECTOR_SIZE;       	// Number of bytes to read in first sector

	
	// Number of bytes to read inside a sector
	int sector_bytes = bytes_to_read < SECTOR_SIZE? bytes_to_read: SECTOR_SIZE - sector_index; 
	
	int buffer_index = 0; 
  
	// Number of sectors to read
	int sectors_to_read = (end_of_read - (SECTOR_SIZE - sector_index)) / SECTOR_SIZE;    

	char buffer[SECTOR_SIZE];
  
	if(open_files[fd].size <= open_files[fd].pos)
	{
         dprintf("Pointer is at EOF\n");
          return 0;
    }

	// Loop through all file data
	for(i = offset_sector_toLoad; i < offset_sector_toLoad + sectors_to_read + 1; i++)
	{       

		 // Check if file too large
		if(i > MAX_SECTORS_PER_FILE)
		{ 
			osErrno = E_FILE_TOO_BIG;
			return -1;              
		}   

		if(Disk_Read(child->data[i], buffer) == 0)
		{       

			//Read from memory to buffer
			if(memcpy(buffer + buffer_index, buffer + sector_index , sector_bytes) == NULL) 
				return -1; 			
       
			// Update file position
			open_files[fd].pos +=  sector_bytes;        
			
			// Update buffer index
			buffer_index += sector_bytes; 
			
			bytes_to_read -= sector_bytes;
			sector_bytes = bytes_to_read < SECTOR_SIZE? bytes_to_read: SECTOR_SIZE; 

		}
		else
		{
			dprintf("Failed to read sector %d\n", child->data[i]);
			osErrno = E_GENERAL; 
			return -1; 
		}    
	}
  
	dprintf("Total bytes read: %d\n", bytes_read );
	
	return bytes_read; 
}









int File_Write(int fd, void* buffer, int size)
{
	dprintf("Writing file...\n");

	// Check if file is open
	if(is_file_open(open_files[fd].inode) != 1)
	{ 
        osErrno = E_BAD_FD;
        return -1;              
	}

	dprintf("open_files.nodes: %d\n", open_files[fd].inode);

	// If file is too big
	if(open_files[fd].size + size > MAX_FILE_SIZE)
	{
		osErrno=E_FILE_TOO_BIG;
		return -1;              
	}
  
	// Get child inode
	int child_inode = open_files[fd].inode;
	int inode_sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
	char inode_buffer[SECTOR_SIZE];
	
	if(Disk_Read(inode_sector, inode_buffer) < 0)
		return -1;
    
	dprintf("Load inode table for child inode from disk sector: %d\n", inode_sector);

	// Get position to start writing to 
	int inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
	int offset = child_inode - inode_start_index;
	
	assert(0 <= offset && offset < INODES_PER_SECTOR);
	
	inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));

	// If inode not a file
	if(child->type != 0) 
	{
		dprintf("ERROR: inode not a file\n");
		osErrno = E_GENERAL;
		return -1;
	}	

	dprintf("Attempting to write inode: %d, size: %d, type: %d\n", child_inode, child->size, child->type);
	
	int offset_sector_toLoad = open_files[fd].pos / SECTOR_SIZE;          
	int sector_index = open_files[fd].pos % SECTOR_SIZE;         	// Number of bytes to write in first sector


	int remainder_bytes = (MAX_FILE_SIZE < open_files[fd].pos + size)? MAX_FILE_SIZE - open_files[fd].pos : size;
	int sector_bytes = SECTOR_SIZE - sector_index;
	int buffer_index = 0;
  
	// Number of sectors we need to write (after initial sector)
	int sectors_to_write = (size - (SECTOR_SIZE - sector_index)) / SECTOR_SIZE;             

	char buffer[SECTOR_SIZE];	// Holds all file data
	
	int i;
  
	// Loop through all file data
	for(i = offset_sector_toLoad; i < sectors_to_write + 1; i++)
	{       
    
		// If file too large
		if(i > MAX_SECTORS_PER_FILE)
		{ 
			osErrno = E_FILE_TOO_BIG;
			return -1;             
		}
    
		// Sector not being used; initialize it
		if(child->data[i] == 0)
		{    
			// Request a new sector
			child->data[i] = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);    
			
			if(child->data[i] < 0) 
			{
				dprintf("ERROR: Disk is full\n");
				osErrno = E_NO_SPACE;
				return -1;
			}
		}
		
		dprintf("Writing bytes into disk sector: %d, index: %d\n" , child->data[i], i);
     
		// Read data from disk
		if(Disk_Read(child->data[i], buffer) == 0)
		{          
			// Copy from buffer to memory sector
			if(memcpy(buffer + sector_index , buffer + buffer_index, sector_bytes) == NULL) 
				return -1;
			
			open_files[fd].pos += sector_bytes;
			buffer_index += sector_bytes;
			remainder_bytes -= sector_bytes;
			sector_bytes = remainder_bytes < SECTOR_SIZE? remainder_bytes: SECTOR_SIZE;

			// Write back to memory sector
			if(Disk_Write(child->data[i], buffer) < 0) 
			{
				dprintf("Failed to write sector: %d\n", child->data[i]); 
				osErrno = E_GENERAL;
				return -1;  
			}

		}
		else
		{
			dprintf("Failed to read sector: %d\n", child->data[i]);
			osErrno = E_GENERAL; 
			return -1; 
		}    
	}

	// Write success
	open_files[fd].size = open_files[fd].pos;
	child->size =  open_files[fd].pos;

	// Write inode sector back to disk
	if(Disk_Write(inode_sector, inode_buffer) < 0) 
	{
        dprintf("Failed to write sector: %d\n", inode_sector);     
		osErrno = E_GENERAL;
        return -1;  
	}
  
	dprintf("Successfully wrote inode sector: %d\n", inode_sector );

    dprintf("Final index of pointer inside file: %d\n", open_files[fd].pos);
	
	return size;
}









int File_Seek(int fd, int offset)
{
	// Check if file open
	if(is_file_open(open_files[fd].inode) != 1)
	{ 
        osErrno = E_BAD_FD;
        return -1; 
	}

	dprintf("File Seek: open_files[%d].size = %d\n",fd, open_files[fd].size);
	
	if(open_files[fd].size < offset || open_files[fd].size < 0)
	{	
		osErrno = E_SEEK_OUT_OF_BOUNDS;
		return -1;
	}
  
	open_files[fd].pos = offset;	
	
	return open_files[fd].pos;  
}







int File_Close(int fd)
{
  dprintf("File_Close(%d):\n", fd);
  if(0 > fd || fd > MAX_OPEN_FILES) {
    dprintf("... fd=%d out of bound\n", fd);
    osErrno = E_BAD_FD;
    return -1;
  }
  if(open_files[fd].inode <= 0) {
    dprintf("... fd=%d not an open file\n", fd);
    osErrno = E_BAD_FD;
    return -1;
  }

  dprintf("... file closed successfully\n");
  open_files[fd].inode = 0;
  return 0;
}






int Dir_Create(char* path)
{
  dprintf("Dir_Create('%s'):\n", path);
  return create_file_or_directory(1, path);
}







int Dir_Unlink(char* path)
{   
	dprintf("Dir_Unlink(%s):\n", path);
  
	int child_inode;
	char last_filename[MAX_NAME];	// last filename
	
	// Get father inode
	int parent_inode = follow_path(path, &child_inode, last_filename);  
  
	// Father found
	if(parent_inode >= 0) 
	{          
		// Child found
		if(child_inode >= 0) 
		{          
			int result;
			
			// Remove the inode
			result = remove_inode(1, parent_inode, child_inode); 
      
			switch(result)
			{
				case 0:   dprintf("Succefully removed inode representing a Directory\n");
						return 0;

				case -1:  dprintf("General error removing inode\n");
						osErrno = E_GENERAL;
						return -1;

				case -2:  dprintf("Current directory not empty\n");
						osErrno = E_DIR_NOT_EMPTY;
						return -1;

				case -3:  dprintf("Wrong type\n");
						osErrno = E_GENERAL;
						return -1;
			}           

		}
		else
		{
			dprintf("Directory %s doesn't exist, delete failed\n", path);
			osErrno = E_NO_SUCH_DIR;
			return -1;
		}  
	}
	else 
	{
		dprintf("ERROR: Invalid directory/path: %s\n", path);
		osErrno = E_NO_SUCH_DIR;
		return -1;
	}
	
	return -1;   
}









int Dir_Size(char* path)
{
	int child_inode;
	
	char last_filename[MAX_NAME];
	
	follow_path(path, &child_inode, last_filename);  
  
	// If child exists
	if(child_inode >= 0) 
	{        
		dprintf("Found file: %s at inode: %d\n", path, child_inode); 
     
		// Load sector that has inode
		int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
		char inode_buffer[SECTOR_SIZE];
		
		if(Disk_Read(inode_sector, inode_buffer) < 0) 
		{ 
			osErrno = E_GENERAL; 
			return -1; 
		}
		
		dprintf("Load inode table for inode from disk sector: %d\n", inode_sector);
	
		int inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
		int offset = child_inode - inode_start_index;
		
		assert(0 <= offset && offset < INODES_PER_SECTOR);
		
		inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
		
		dprintf("Inode: %d, size: %d, type: %d\n", child_inode, child->size, child->type);

		// If inode is a file
		if(child->type == 0) 
		{     
			dprintf("ERROR: inode found is a file, not a directory: %s\n", path);
			osErrno = E_GENERAL;
			return -1;
		}

		// We know inode is a directory, so return it's size
		return child->size * (sizeof(dirent_t));

	}
	else 
	{
		dprintf("Couldn't find file: %s\n", path);
		osErrno = E_GENERAL;
		return -1;
	}	     
}









int Dir_Read(char* path, void* buffer, int size)
{
 
	//First we need to get the child inode referenced by path
	int child_inode;
	char last_filename[MAX_NAME];
	
	follow_path(path, &child_inode, last_filename);  
	
	int counter = 0;              //This counter will keep track of how many dirent we have visited
  
	dirent_t* current_dirent;

	// If child exists
	if(child_inode >= 0) 
	{        
		// Check if size big enough for dirent objects
		if(size < Dir_Size(path))
		{      
			dprintf("Buffer size: %d is too small for dierectory\n", size);
			osErrno = E_BUFFER_TOO_SMALL;
			return -1;
		}

		// Load disk sector that has inode
		int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
		char inode_buffer[SECTOR_SIZE];
		
		if(Disk_Read(inode_sector, inode_buffer) < 0) 
		{ 
			osErrno = E_GENERAL; 
			return -1; 
		}
      
		dprintf("Load inode table for inode from disk sector: %d\n", inode_sector);

		// Get inode
		int inode_start_index = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
		int offset = child_inode - inode_start_index;
		
		assert(0 <= offset && offset < INODES_PER_SECTOR);
		
		inode_t* child = (inode_t*)(inode_buffer + offset * sizeof(inode_t));
		
		dprintf("Inode: %d, size: %d, type: %d\n", child_inode, child->size, child->type);

     
		
		int i;
    
		for(i = 0; i < MAX_SECTORS_PER_FILE; i++)
		{     
			char data_buffer[SECTOR_SIZE];
        
			// Read data from disk to sector
			if(Disk_Read(child->data[i], data_buffer) < 0) 
			{ 
				osErrno = E_GENERAL; 
				return -1; 
			}  
			
			dprintf("Load data from disk sector: %d\n", child->data[i]);
			
			int j;
			
			// Loop through all dirents in the directory
			for(j = 0; ((j < DIRENTS_PER_SECTOR) && (counter < child->size)); j++)
			{   
				current_dirent = (dirent_t*)(data_buffer + j * sizeof(dirent_t));              
              
				if(memcpy(buffer + j *(sizeof(dirent_t)), current_dirent, sizeof(dirent_t)) == NULL) 
					return -1;                                                                      
              
				counter++;        
			}
        
			if(counter == child->size)
			{          
				return child->size;
			} 
		}
	}
	else 
	{
		dprintf("Couldn't find file: %s\n", path);
		osErrno = E_GENERAL;
		return -1;
	}     

	return -1;
}