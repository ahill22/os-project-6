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
    struct disk        *disk;
    bool        *free_blocks;
    fs_superblock  meta_data;
};

FileSystem *fs;

int fs_format()
{

    union fs_block block;

    block.super.magic = FS_MAGIC;
	int b = disk_nblocks(thedisk);
	block.super.nblocks = b;

    if(b % 10) {
        block.super.ninodeblocks = (block.super.nblocks / 10) + 1;
    }
    else {
        block.super.ninodeblocks = (block.super.nblocks / 10);
    }

    block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

    disk_write(thedisk, 0, block.data);
    
	union fs_block clear = {{0}};

	int i;
    for(i = 1; i < b; i++) {
        disk_write(thedisk, i, clear.data);
    }

	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(thedisk,0,block.data);

	printf("superblock:\n");

	if(block.super.magic == FS_MAGIC) {
        printf("    magic number is valid\n");
    }
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	union fs_block inodeBlock;
	int i, j, k, l;
	for(i=1; i<block.super.ninodeblocks; i++){
		disk_read(thedisk, i, inodeBlock.data);
		for(j=0; j<INODES_PER_BLOCK; j++){
			if(inodeBlock.inode[j].isvalid){
				
				printf("Inode %d:\n", j+(i-1)*INODES_PER_BLOCK);
				printf("    size: %u bytes\n", inodeBlock.inode[j].size);
				printf("    direct blocks:");
				for(k=0; k<POINTERS_PER_INODE; k++){
					if(inodeBlock.inode[j].direct[k]){
						printf(" %u", inodeBlock.inode[j].direct[k]);
					}
				}
				printf("\n");
				
				if(!inodeBlock.inode[j].indirect){
					continue;
				}
				union fs_block indirect;
				disk_read(thedisk, inodeBlock.inode[j].indirect, indirect.data);
				printf("   indirect block: %u\n", inodeBlock.inode[j].indirect);
				printf("    indirect data blocks:");

				for(l=0; l<POINTERS_PER_BLOCK; l++){
					if(indirect.pointers[l]){
						printf(" %u\n", indirect.pointers[l]);
					}
				}
				printf("\n");
			}
		}
	}
}

int fs_mount()
{
	
	/*if(!fs || fs->disk || !thedisk){
		printf("mount about to fail\n");
		return 0;
	}*/

    union fs_block block;
    disk_read(thedisk, 0, block.data);
	int b = disk_nblocks(thedisk);

    if(block.super.magic != FS_MAGIC || block.super.nblocks != b || block.super.ninodes != (block.super.ninodeblocks * INODES_PER_BLOCK)) {
    	//printf("mount about to fail\n");
		return 0;
    }

    if(!(b % 10)) {
        if(block.super.ninodeblocks < (b / 10)) {
            return 0;
        }
    }
    else if(block.super.ninodeblocks < (b / 10) + 1) {
        return 0;
    }
	fs = 0;
    fs->meta_data = block.super;

    fs->disk = thedisk;

    fs->free_blocks = calloc(fs->meta_data.nblocks, sizeof(bool));


	int i, j, k, l, m;
    for( i = fs->meta_data.ninodeblocks + 1; i < fs->meta_data.nblocks; i++) {
        fs->free_blocks[i] = true;
    }

    for( j = 1; j <= fs->meta_data.ninodeblocks; j++) {
        union fs_block curr;
        disk_read(fs->disk, j, curr.data);

        for( k = 0; k < INODES_PER_BLOCK; k++) {
            if(!curr.inode[k].isvalid) {
                continue;
            }

            for( l = 0; l < POINTERS_PER_INODE; l++) {
                if(curr.inode[k].direct[l]) {
                    fs->free_blocks[curr.inode[k].direct[l]] = false;
                }
            }

            if(!curr.inode[k].indirect) {
                continue;
            }

            fs->free_blocks[curr.inode[k].indirect] = false;

            union fs_block indirect_block;
            disk_read(fs->disk, curr.inode[k].indirect, indirect_block.data);

            for( m = 0; m < POINTERS_PER_BLOCK; m++) {
                if(indirect_block.pointers[m]) {
                    fs->free_blocks[indirect_block.pointers[m]] = false;
                }
            }
        }
    }

	return 0;
}

int fs_create()
{
	union fs_block b;
	int i, j, k;
    for ( i = 1; i <= fs->meta_data.ninodeblocks; i++) {
        disk_read(fs->disk, i, b.data);


        for ( j = 0; j < INODES_PER_BLOCK; j++) {
            if(!b.inode[j].isvalid) {

                b.inode[j].isvalid = 1;
                for ( k = 0; k < POINTERS_PER_INODE; k++) {
                    b.inode[j].direct[k] = 0;
                }

                b.inode[j].indirect = 0;
                b.inode[j].size = 0;

                disk_write(fs->disk, i, b.data);

                return (j + ((i-1) * INODES_PER_BLOCK));
            }
        }
    }
	return 0;
}

int fs_delete( int inumber )
{
	union fs_block b;
    size_t inode_index = inumber % INODES_PER_BLOCK;
    size_t inode_block = (inumber / INODES_PER_BLOCK) + 1;

    disk_read(fs->disk, inode_block, b.data);

    if(!b.inode[inode_index].isvalid) {
        return 0;
    }
	int i, j;
    for( i = 0; i < POINTERS_PER_INODE; i++) {
        if(b.inode[inode_index].direct[i]) {
            fs->free_blocks[b.inode[inode_index].direct[i]] = true;
            b.inode[inode_index].direct[i] = 0;
        }
    }

    if(b.inode[inode_index].indirect) {
        union fs_block indirect_block;
        disk_read(fs->disk, b.inode[inode_index].indirect, indirect_block.data);

        for( j = 0; j < POINTERS_PER_BLOCK; j++) {

            if(indirect_block.pointers[j]) {
                fs->free_blocks[indirect_block.pointers[j]] = true;
                indirect_block.pointers[j] = 0;
            }
        }

        fs->free_blocks[b.inode[inode_index].indirect] = true;
        b.inode[inode_index].indirect = 0;
    }

    b.inode[inode_index].isvalid = 0;
    b.inode[inode_index].size = 0;

    disk_write(fs->disk, inode_block, b.data);

	return 0;
}

int fs_getsize( int inumber )
{
	union fs_block b;
	int i, j;
    for ( i = 1; i <= fs->meta_data.ninodeblocks; i++) {

        disk_read(fs->disk, i, b.data);

		for ( j = 0; j < INODES_PER_BLOCK; j++) {
			int curr_inode = j + (i-1) * INODES_PER_BLOCK;

			if (inumber == curr_inode) {

				if (b.inode[j].isvalid) {
					return b.inode[j].size;
				}
				else {
					return -1;
				}
			}
		}
    }
    return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
