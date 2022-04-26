/*
Implementation of SimpleFS.
Make your changes here.
*/

#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern struct disk *thedisk;

#define FS_MAGIC           0x30341003
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 3
#define POINTERS_PER_BLOCK 1024

typedef struct fs_superblock fs_superblock;
struct fs_superblock {
	int32_t magic;
	int32_t nblocks;
	int32_t ninodeblocks;
	int32_t ninodes;
};

struct fs_inode {
	int32_t isvalid;
	int32_t size;
	int64_t ctime;
	int32_t direct[POINTERS_PER_INODE];
	int32_t indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	unsigned char data[BLOCK_SIZE];
};

typedef struct FileSystem FileSystem;
struct FileSystem {
    struct disk *disk;
    bool *free_blocks;
    struct fs_superblock meta;
};

int32_t fs_allocate_free_block();
void inode_load(int inumber, struct fs_inode *inode);
void inode_save(int inumber, struct fs_inode *inode);

//FileSystem *fs;
FileSystem fs = {0};

int fs_format()
{
    if(fs.disk){
        printf("disk is already mounted\n");
        return 0;
    }

    //create super block
    union fs_block sblock = {{0}};
    sblock.super.magic = FS_MAGIC;
	int b = disk_nblocks(thedisk);
    //printf("%d\n", b);
	sblock.super.nblocks = b;
    //printf("%d\n", block.super.nblocks);

    //set aside 10% for inode blocks
    if(b % 10) {
        //printf("1: %d\n", (block.super.nblocks / 10) + 1);
        sblock.super.ninodeblocks = (sblock.super.nblocks / 10) + 1;
    }
    else {
        //printf("2: %d\n",(block.super.nblocks / 10));
        sblock.super.ninodeblocks = (sblock.super.nblocks / 10);
    }
    //set total number of inodes
    sblock.super.ninodes = sblock.super.ninodeblocks * INODES_PER_BLOCK;

    disk_write(thedisk, 0, sblock.data);
    
    //clear the inode bitmap
    struct fs_inode inode;
    memset(&inode, 0, sizeof(inode)); //set inode to 0
	int i;
    for(i = 1; i < sblock.super.ninodes; i++) {
        inode_save(i, &inode); //save all inodes as 0
    }
	return 1;
}

void fs_debug()
{
	union fs_block block;

    //disk read error checks for us 
	disk_read(thedisk, 0, block.data); //read superblock

    //print superblock info
	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int i, k, l;

    //scan inodes 
	for(i=0; i < block.super.ninodeblocks * INODES_PER_BLOCK; i++){
        //printf("hi\n");
        //create inode and load the desired inode
        struct fs_inode inode;
        inode_load(i, &inode);

        //print out inode info if it's valid
		if(inode.isvalid){
            printf("Inode %d:\n", i);
            printf("    size: %u bytes\n", inode.size);
            printf("    created: %s", ctime(&inode.ctime));
            printf("    direct blocks:");
            for(k=0; k<POINTERS_PER_INODE; k++){
                if(inode.direct[k]){
                    printf(" %u", inode.direct[k]);
                }
            }
			printf("\n");
            //check for indirect blocks
            if(inode.indirect){	
                union fs_block indirect;
                disk_read(thedisk, inode.indirect, indirect.data);
                //print out indirect blocks info
                printf("   indirect block: %u\n", inode.indirect);
                printf("   indirect data blocks:");

                for(l=0; l<POINTERS_PER_BLOCK; l++){
                    if(indirect.pointers[l]){
                        printf(" %u", indirect.pointers[l]);
                    }
                }
                printf("\n");
            }
        }
		
	}
}

int fs_mount()
{

    union fs_block block;
    disk_read(thedisk, 0, block.data); //read superblock 
	int b = disk_nblocks(thedisk);
    //error check
    if(block.super.magic != FS_MAGIC || block.super.nblocks != b || block.super.ninodes != (block.super.ninodeblocks * INODES_PER_BLOCK)) {
		return 0;
    }
    //check that there are the correct number of inode blocks
    if(!(b % 10)) {
        if(block.super.ninodeblocks < (b / 10)) {
            return 0;
        }
    }
    else if(block.super.ninodeblocks < (b / 10) + 1) {
        return 0;
    }
	
    // preparing fs for use
    fs.meta = block.super;
    fs.disk = thedisk;

    //allocate space for bitmap 
    fs.free_blocks = calloc(fs.meta.nblocks, sizeof(bool));
    if(!fs.free_blocks){
        printf("Calloc failed\n");
        return 0;
    }
    //set super blcok and inode blocks to false right away 
    //iterate through all inodes and do inode load. go through direct and indirects and check if the blocks are valid, then set to zero for bit map
    
	int i, j, k;
    //initialize bitmap
    for( i = fs.meta.ninodeblocks + 1; i < fs.meta.nblocks; i++) {
        fs.free_blocks[i] = true;
    }
    //inodes already in use
    for(i= 0; i < fs.meta.ninodeblocks +1; i++){
        fs.free_blocks[i] = false;
    }
    
    for(i = 0; i < fs.meta.ninodes; i++) {
        struct fs_inode inode;
        inode_load(i, &inode);
        //iterate though the pointers in the inode
        for(j = 0; j < POINTERS_PER_INODE; j++) {
            if(inode.direct[j]) {
                fs.free_blocks[inode.direct[j]] = false; //mark the blocks in use
            }
        }
        //check for indirect blocks
        if(inode.indirect) {        
            fs.free_blocks[inode.indirect] = false;

            union fs_block indirect_block;

            disk_read(thedisk, inode.indirect, indirect_block.data);
            //printf("hi\n");
            //iterate through the pointers in each block
            for(k = 0; k < POINTERS_PER_BLOCK; k++) {
                if(indirect_block.pointers[k]) {
                    fs.free_blocks[indirect_block.pointers[k]] = false; //mark the bitmap for blocks in use
                }
            }
        }
    }
	return 1;
}

int fs_create()
{
    if(!fs.disk){
        printf("not mounted\n");
        return 0;
    }
    int i;
    for( i = 1; i < fs.meta.ninodeblocks * INODES_PER_BLOCK; i++) {
        struct fs_inode inode;
        inode_load(i, &inode);
        
        //if already valid/exists continue to the next one
        if(inode.isvalid){
            continue;
        }
        inode.ctime = time(0);
        inode.size = 0;
        inode.isvalid = true;
        int inumber = i;
        
        inode_save(inumber, &inode);
        
        // update superblock
        union fs_block b;
        disk_read(thedisk, 0, b.data);
        //printf("inum %d\n", inumber);
        return inumber;
    }
    
    return 0;
}

int fs_delete( int inumber )
{
    if(!fs.disk){
        printf("not mounted\n");
        return 0;
    }
    // error check
    if(inumber < 0 || inumber > fs.meta.ninodes){
        printf("invalid inumber\n");
        return 0;
    } 

    struct fs_inode inode;
    inode_load(inumber, &inode);

    //error check
    if(!inode.isvalid){
        printf("invalid inode\n");
        return 0;
    }

    // remove direct blocks
    inode.isvalid = false;
    int i;
    for( i = 0; i < POINTERS_PER_INODE; i++) {
        fs.free_blocks[inode.direct[i]] = true; //update the bitmap
        inode.direct[i] = 0;
        inode.size = 0;
    }
    
    //remove indirect blocks
    if(inode.indirect) {
        union fs_block indirect;

        disk_read(thedisk, inode.indirect, indirect.data);

        for(i = 0; i < POINTERS_PER_BLOCK; i++) {
            if(indirect.pointers[i]) {
                fs.free_blocks[indirect.pointers[i]] = true; //update the bitmap 
                indirect.pointers[i] = 0;
            }
        }

        fs.free_blocks[inode.indirect] = true;
        inode.indirect = 0;
    }

    inode.size = 0;
    inode_save(inumber, &inode);
    
    return 1;
}

int fs_getsize( int inumber )
{
     if(!fs.disk){
        printf("not mounted\n");
        return 0;
    }
    struct fs_inode inode;
    inode_load(inumber, &inode);

    if(inumber >= fs.meta.ninodes){
        printf("invalid inumber\n");
        return -1;
    }
    if(!inode.isvalid){
        printf("invalid inode\n");
        return -1;
    }
    if(inode.size >= 0){
        return inode.size; //returns the size of the inode in bytes
    } 
    return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
    if(!fs.disk){
        printf("not mounted\n");
        return 0;
    }
   // printf("entering fs_read\n");
    if(inumber < 0)
    {
        printf("invalid inumber\n");
        return 0;
    }
    if(fs.meta.ninodes < inumber)
    {
        printf("invalid inumber\n");
        return 0;
    }

    struct fs_inode inode;
    inode_load(inumber, &inode);
    if(!inode.isvalid) //ensures inode is valid
    {
        printf("invalid inode\n");
        return 0;
    }
    
    //adjust the length based on size of the inode
    if (length > inode.size){
        length = inode.size - offset;
    }
    if(inode.size < offset || length <= 0){
        return 0;
    }

    int bytes = 0;
    int i;
    for(i = offset/BLOCK_SIZE; i < POINTERS_PER_INODE; i++) {
        union fs_block direct;
        disk_read(thedisk, inode.direct[i], direct.data);
        if((length - bytes) < BLOCK_SIZE) {
            memcpy(data + bytes, &direct.data, length - bytes);
            bytes = length;
            return bytes;
        }
        else {
            memcpy(data + bytes, &direct.data, BLOCK_SIZE);
            bytes = bytes + BLOCK_SIZE;
        }
    }

    uint32_t read_size = inode.size;
    if((read_size - offset) < length){
        length = read_size - offset;
    }

    // indirect blocks
    union fs_block indirect;
    if(inode.indirect) {            
        uint32_t initial_block = (offset - (POINTERS_PER_INODE * BLOCK_SIZE))/ BLOCK_SIZE;

        for( i = initial_block; i < POINTERS_PER_BLOCK; i++) {
            disk_read(thedisk, indirect.pointers[i], indirect.data);

            if((length - bytes) < BLOCK_SIZE) {
                memcpy(data + bytes, &indirect.data, length - bytes);
                bytes = length;
                return bytes;
            }
            else {
                memcpy(data + bytes, &indirect.data, BLOCK_SIZE);
                bytes = bytes + BLOCK_SIZE;
            }
        }
    }
    return bytes;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    if(!fs.disk){
        printf("not mounted\n");
        return 0;
    }
    struct fs_inode inode;
    inode_load(inumber, &inode);

    if(!inode.isvalid || offset > inode.size){
        printf("invalid inode\n");
        return 0;
    }
    
    if((length + offset) > ((POINTERS_PER_INODE * BLOCK_SIZE) + (POINTERS_PER_BLOCK * BLOCK_SIZE))) {
        return 0;
    }

    int writeBlock; //write blocks
    int nPointer = offset/BLOCK_SIZE; //how many blocks we need
    int bytes = 0;
    int a = offset % BLOCK_SIZE;

    while(length > bytes) {
        union fs_block readBlock = {{0}};
        int pointer = 0;

        // direct blocks
        if(nPointer < POINTERS_PER_INODE) {
            pointer = nPointer;
            if(inode.direct[pointer]){
                writeBlock = inode.direct[pointer];
            } 
            else {
                int i;
                int freeBlock = -1;
                for( i = 0; i < disk_nblocks(thedisk); i++) {
                    if(fs.free_blocks[i]) {
                        freeBlock = i;
                        fs.free_blocks[freeBlock] = false;
                        break;
                    }
                }

                if(freeBlock == -1) break;

                writeBlock = freeBlock;
            }

            int i;
            for(i = a; i < BLOCK_SIZE; i++) {
                if(bytes >= length) break;
                readBlock.data[i] = data[bytes];
                bytes++;
            
            disk_write(thedisk, writeBlock, readBlock.data);

            inode.direct[pointer] = writeBlock;
            }
        }

        // indirect blocks
        else if(nPointer < (POINTERS_PER_BLOCK + POINTERS_PER_INODE)) {
            int indirectNum = 0;

            if(inode.indirect){
                indirectNum = inode.indirect;
            }

            else {
                int freeBlock = -1;
                int i;
                for(i = 1; i < fs.meta.nblocks; i++) {
                    if(fs.free_blocks[i]) {
                        freeBlock = i;
                        fs.free_blocks[freeBlock] = false;
                        break;
                    }
                }

                if(freeBlock == -1) break;

                union fs_block zeroBlock = {{0}};      

                disk_write(thedisk, freeBlock, zeroBlock.data);

                indirectNum = freeBlock;
                inode.indirect = indirectNum;
            }

            union fs_block indirect;
            disk_read(thedisk, inode.indirect, indirect.data);

            pointer = indirect.pointers[nPointer - POINTERS_PER_INODE];

            if(!pointer) {
                int freeBlock = -1;
                int i;
                for(i = 1; i < fs.meta.nblocks; i++) {
                    if(fs.free_blocks[i]) {
                        freeBlock = i;
                        fs.free_blocks[freeBlock] = false;
                        break;
                    }
                }

                if(freeBlock < 0)
                    break;

                writeBlock = freeBlock;     
            }

            else{
                writeBlock = pointer;     
            }

            int i;
            for(i = a; i < BLOCK_SIZE; i++) {
                if(bytes >= length)
                    break;
                readBlock.data[i] = data[bytes];
                bytes++;
            }

            indirect.pointers[nPointer - POINTERS_PER_INODE] = writeBlock;
        
            disk_write(thedisk, writeBlock, readBlock.data);
            disk_write(thedisk, indirectNum, indirect.data);
        }

        nPointer++;     
    }

    inode.isvalid = true;
    inode.size = bytes + offset;

    inode_save(inumber, &inode);

    return bytes;
}

void inode_load(int inumber, struct fs_inode *inode){    
    //printf("in inode load\n");
    union fs_block block;
    int blockNum = (inumber / INODES_PER_BLOCK) + 1; //plus 1 skips the super block 
    int offset = inumber % INODES_PER_BLOCK;

    disk_read(thedisk, blockNum, block.data);
    *inode = block.inode[offset];
}

void inode_save(int inumber, struct fs_inode *inode) {

    union fs_block block;
    int blockNum = (inumber / INODES_PER_BLOCK) + 1;
    int offset = inumber % INODES_PER_BLOCK;

    disk_read(thedisk, blockNum, block.data);

    block.inode[offset] = *inode;
    disk_write(thedisk, blockNum, block.data);
}
