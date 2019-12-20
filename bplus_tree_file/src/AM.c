#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AM.h"
#include "bf.h"

int AM_errno = AME_OK;

struct file_info{
    char* fileName;
    int fileDesc;
    int rootBlock;
    char attrType1;
    char attrType2;
    int attrLength1;
    int attrLength2;
};

struct scan_info{
    int lastRecord_block;
    int lastRecord_position;
    int fileDesc;
};

struct file_info Files_array[MAX_OPEN_FILES];
struct scan_info Scans_array[MAX_OPEN_SCANS];

int file = -1;

int node_offset = sizeof(char)+sizeof(int);
int leaf_offset = sizeof(char)+sizeof(int)*3;

/**
 * AM_Init()
 *  returns: nothing
 *
 * This function is used to initialize the global structures that are needed for the usage
 * of the B+ Tree.
 */
void AM_Init() {

    /* Test correct behaviour of BF_Init */
    if (BF_Init(LRU) != BF_OK){
        AM_errno = AME_INIT; 

        AM_PrintError("Error while initializing the BF level.");
        exit(AM_errno);
    }

    /* Initiallize the File_array */
    for (int i = 0; i<MAX_OPEN_FILES; i++){
        Files_array[i].fileName = NULL;
        Files_array[i].fileDesc = -1;
        Files_array[i].rootBlock = 0;
        Files_array[i].attrType1 = 'l';
        Files_array[i].attrType2 = 'l';
        Files_array[i].attrLength1 = -1;
        Files_array[i].attrLength2 = -1;
    }

    for(int i = 0; i<MAX_OPEN_SCANS; i++){
        Scans_array[i].lastRecord_block = -1;
        Scans_array[i].lastRecord_position = -1;
        Scans_array[i].fileDesc = -1;
    }
	return;
}

/**
 * AM_CreateIndex(char *fileName, char attrType1, int attrLength1, char attrType2, int attrLength2)
 *  returns: AME_OK - if it succeeds, Some other error code - if it fails
 *
 * This function creates a file with name fileName, that is based on a B+ Tree. The file must not
 * already exist. The type and length of the first field(which is used for the insertion in the B+
 * Tree as a key) are described by the second and third parameter, correnspondingly. Samewise, the
 * type and the length of the second field are described by the fourth and fifth parameter.
 */
int AM_CreateIndex(char *fileName, 
	               char attrType1, 
	               int attrLength1, 
	               char attrType2, 
	               int attrLength2) {

    if (BF_CreateFile(fileName) != BF_OK){
        AM_errno = AME_CREATE_FILE;
        AM_PrintError("Error while creating the file.");
        exit(AM_errno);
    }
    
    int fileDesc;
    if(BF_OpenFile(fileName, &fileDesc) != BF_OK){
        AM_errno = AME_OPEN_FILE;
        AM_PrintError("Error while openning the file.");
        exit(AM_errno);
    }

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;
    if(BF_AllocateBlock(fileDesc, block) != BF_OK){
        AM_errno = AME_ALLOCATE;
        AM_PrintError("Error while allocating a block.");
        exit(AM_errno);
    }

    /* Make sure that there is only one block allocated in the file. */
    int blocks_num;
    if(BF_GetBlockCounter(fileDesc, &blocks_num) != BF_OK){
        AM_errno = AME_COUNTER;
        AM_PrintError("Error while calling BF_GetBlockCounter.");
        exit(AM_errno);
    }

    if(blocks_num != 1){
        AM_errno = AME_BLOCKS;
        AM_PrintError("Error due to the number of blocks.");
        exit(AM_errno);
    }

    if(BF_GetBlock(fileDesc, 0, block) != BF_OK){
        AM_errno = AME_GETBLOCK;
        AM_PrintError("Error while getting a block.");
        exit(AM_errno);
    }
    data = BF_Block_GetData(block);

    char type = 'b';
    int zero = 0;
    memcpy(data, &type, sizeof(char));
    memcpy(data+sizeof(char), &attrType1, sizeof(char));
    memcpy(data+sizeof(char)*2, &attrLength1, sizeof(int));
    memcpy(data+sizeof(char)*2+sizeof(int), &attrType2, sizeof(char));
    memcpy(data+sizeof(char)*3+sizeof(int), &attrLength2, sizeof(int));
    memcpy(data+sizeof(char)*3+sizeof(int)*2, &zero, sizeof(int));

    /*
     * The first block presentation:
     *
     * ['b'(shows that the file implements a B+Tree), 
     * attrType1,
     * attrLength1,
     * attrType2,
     * attrLength2, 
     * 0(shows which block has the root of the B+Tree)]
     */

    BF_Block_SetDirty(block);
    if(BF_UnpinBlock(block) != BF_OK){
        AM_errno = AME_UNPIN;
        AM_PrintError("Error while unpining a block");
        exit(AM_errno);
    }

    BF_Block_Destroy(&block);
    BF_CloseFile(fileDesc);
    return AME_OK;
}

/**
 * AM_DestroyIndex(char *fileName)
 *  returns: AME_OK - if it succeeds, Some other error code - if it fails
 *
 * This function destroys the file with name fileName, deleting the file from the disk.
 * The file cannot be deleted if opens of it exist in the Files_array.
 */
int AM_DestroyIndex(char *fileName) {
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(strcmp(Files_array[i].fileName, fileName) == 0){
            AM_errno = AME_DESTROY;
            AM_PrintError("Error while destroying the file.");
            exit(AM_errno);
        }
    }

    if(remove(fileName) == 0) {
        printf("File removed successfully.");
        return AME_OK;
    } else {
        AM_errno = AME_REMOVE;
        AM_PrintError("Error while removing the file.");
        exit(AM_errno);
    }

  return AME_OK;
}

/**
 * AN_OpenIndex(char *fileName)
 *  returns: integer - Position in the Files_array that the file is opened
 *           Some error code - if it fails.
 *
 * This function opens the file with name fileName. If the file is normally
 * opened, the function returns a small, non-negative integer, which is used
 * to recognize the file. In any other case, it returns an error code.
 *
 * There is a Files_array keeped in the memory for all the opened files. The
 * integer that is returned by the AM_OpenIndex is the position of the table
 * that corrensponds to the file that was just opened. The same file may be
 * opened many times and for each time it occupies a different position in the
 * table.
 */
int AM_OpenIndex (char *fileName) {
    int position = -1;
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(Files_array[i].fileDesc == -1){
            position = i;
            Files_array[i].fileName = (char*)malloc(sizeof(char)*strlen(fileName));
            strcpy(Files_array[i].fileName, fileName);

            int fileDesc;
            if(BF_OpenFile(fileName, &fileDesc) != BF_OK){
                AM_errno = AME_OPEN_FILE;
                AM_PrintError("Error while opening file.");
                exit(AM_errno);
            }
            Files_array[i].fileDesc = fileDesc;

            BF_Block *block;
            BF_Block_Init(&block);
            char *data;
            
            if(BF_GetBlock(fileDesc, 0, block) != BF_OK){
                AM_errno = AME_GETBLOCK;
                AM_PrintError("Error while getting a block.");
                exit(AM_errno);
            }
            
            data = BF_Block_GetData(block);
            int root;
            char attrType1, attrType2;
            int attrLength1, attrLength2;
            memcpy(&root, data+sizeof(char)*3+sizeof(int)*2, sizeof(int));
            memcpy(&attrType1, data+sizeof(char), sizeof(char));
            memcpy(&attrLength1, data+sizeof(char)*2, sizeof(char));
            memcpy(&attrType2, data+sizeof(char)*2+sizeof(int), sizeof(char));
            memcpy(&attrLength2, data+sizeof(char)*3+sizeof(int), sizeof(int));

            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                AM_PrintError("Error while unpining a block.");
                exit(AM_errno);
            }

            Files_array[i].rootBlock = root;
            Files_array[i].attrType1 = attrType1;
            Files_array[i].attrType2 = attrType2;
            Files_array[i].attrLength1 = attrLength1;
            Files_array[i].attrLength2 = attrLength2;

            BF_Block_Destroy(&block);
            break;
        }
    }

    if(position != -1){
        return position;
    } else {
        AM_errno = AME_OPENINDEX;
        AM_PrintError("Error while trying AM_OpenIndex.");
        exit(AM_errno);
    }
}

/**
 * AM_CloseIndex(int fileDesc)
 *  returns: AME_OK - if it succeeds, Some error code - if it fails.
 *
 * This function closes the file that is defined by its parameter. It also
 * removes the entry that corrensponds to that file from the Files_array. In
 * order for the file that is defined by the parameter fileDesc to be closed
 * successfully, there must not be opened scans of it.
 */
int AM_CloseIndex (int fileDesc) {
    for(int i = 0; i < MAX_OPEN_SCANS; i++){
        if(Scans_array[i].fileDesc == fileDesc){
            AM_errno = AME_OPEN_SCAN;
            AM_PrintError("Error while closing an indexed file.");
            exit(AM_errno);
        }
    }
    
    int flag = 0;
    for(int i=0 ; i<MAX_OPEN_FILES; i++){
        if(Files_array[i].fileDesc == fileDesc){
            free(Files_array[i].fileName);
            Files_array[i].fileDesc = -1;
            Files_array[i].rootBlock = -1;
            Files_array[i].attrType1 = 'l';
            Files_array[i].attrType2 = 'l';
            Files_array[i].attrLength1 = -1;
            Files_array[i].attrLength2 = -1;
            flag = 1;
        }
    }

    if(flag == 1){
        if(BF_CloseFile(fileDesc) != BF_OK){
            AM_errno = AME_CLOSE;
            AM_PrintError("Error while closing the file.");
            exit(AM_errno);
        }
    }else{
        AM_errno = AME_CLOSE_NOT_EXIST;
        AM_PrintError("Error while closing the file.");
        exit(AM_errno);
    }

    return AME_OK;
}

/**
 * AM_InsertEntry(int fileDesc, void* value1, void* value2)
 *  returns: AME_OK - if it succeeds, Some error code - if it fails.
 *
 * This function inserts the pair(value1, value2) at the file that is pointed by the
 * parameter fileDesc. The parameter value1 points to the value of the key-field that
 * is inserted to the file and the value2 represents the other field of the record.
 */
int AM_InsertEntry(int fileDesc, void *value1, void *value2) {
    int flag = 0;
    for(int i = 0; i< MAX_OPEN_FILES; i++){
        if(Files_array[i].fileDesc == fileDesc){
            int root = Files_array[i].rootBlock;
            int max_entries = (BF_BLOCK_SIZE-leaf_offset)/(Files_array[i].attrLength1+Files_array[i].attrLength2+sizeof(int));
            if(max_entries%2 == 1){
                max_entries--;
            }
            //char attrType1 = Files_array[i].attrType1;
            //char attrType2 = Files_array[i].attrType2;
            //int attrLength1 = Files_array[i].attrLength1;
            //int attrLength2 = Files_array[i].attrLength2;
            
            file = i;

            int blocks_num;
            BF_Block *block;
            BF_Block_Init(&block);
            char *data;

            if(BF_GetBlockCounter(fileDesc, &blocks_num) != BF_OK){
                AM_errno = AME_BLOCKS;
                AM_PrintError("Error while getting block counter.");
                exit(AM_errno);
            }

            if(blocks_num == 1){
                /* In this case, this is the first entry inserted in the file.
                 * We need to allocate a new block, which will be a root as well as a leaf node.
                 */
                if(BF_AllocateBlock(fileDesc, block) != BF_OK){
                    AM_errno = AME_ALLOCATE;
                    AM_PrintError("Error while allocating a block.");
                    exit(AM_errno);
                }
                char type = 'o';
                int entries = 1;
                int next_leaf = -1;
                int prev_leaf = -1;
                int entry_offset = -1;

                data = BF_Block_GetData(block);
                memcpy(data, &type, sizeof(char));
                memcpy(data+sizeof(char), &entries, sizeof(int));
                memcpy(data+sizeof(char)+sizeof(int), &next_leaf, sizeof(int));
                memcpy(data+sizeof(char)+sizeof(int)*2, &prev_leaf, sizeof(int));

                for(int i = 0; i < max_entries; i++){
                    memcpy(data+leaf_offset+i*sizeof(int), &entry_offset, sizeof(int));
                }

                memcpy(data+leaf_offset+max_entries*sizeof(int), value1, Files_array[i].attrLength1);
                memcpy(data+leaf_offset+max_entries*sizeof(int)+Files_array[i].attrLength1, value2, Files_array[i].attrLength2);

                entry_offset = leaf_offset+max_entries*sizeof(int);
                memcpy(data+leaf_offset, &entry_offset, sizeof(int));

                BF_Block_SetDirty(block);
                if(BF_UnpinBlock(block) != BF_OK){
                    AM_errno = AME_UNPIN;
                    AM_PrintError("Error while unpining a block.");
                    exit(AM_errno);
                }
                return AME_OK;
            }

            void *newchildentry = NULL;
            int result = insertEntry(file, root, value1, value2, newchildentry);

            if(result != 1){
                AM_errno = AME_INSERT_ERROR;
                AM_PrintError("Error while inserting an entry.");
                exit(AM_errno);
            }
            flag = 1;
            break;
        }
    }
    if(flag == 0){
        AM_errno = AME_FILE_DESC_NOT_FOUND;
        AM_PrintError("Error while inserting an entry.");
        exit(AM_errno);
    }
  return AME_OK;
}

int insertEntry(int fileIndex, int nodePointer, void *value1, void *value2, void *newchildentry){
    int blocks_num;
    int entry_size = Files_array[fileIndex].attrLength1+Files_array[fileIndex].attrLength2;
    int max_entries = (BF_BLOCK_SIZE-leaf_offset)/(Files_array[fileIndex].attrLength1+Files_array[fileIndex].attrLength2+sizeof(int));
    if(max_entries%2 == 1){
        max_entries--;
    }
    int d = max_entries/2;

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;

    if(BF_GetBlock(Files_array[fileIndex].fileDesc, nodePointer, block) != BF_OK){
        AM_errno = AME_GETBLOCK;
        AM_PrintError("Error while getting a block.");
        exit(AM_errno);
    }

    data =  BF_Block_GetData(block);
    char type;
    int entries;

    memcpy(&type, data, sizeof(char));
    memcpy(&entries, data+sizeof(char), sizeof(int));

    if(type == 'o' || type == 'l'){
        if(entries == max_entries){
            /* There is no space in this leaf node (L)
             * Split L: first d entries stay, rest move to brand new node L2
             */
            int next_leaf_id;
            memcpy(&next_leaf_id, data+sizeof(char)+sizeof(int), sizeof(int));

            char new_type = 'l'; 
            if(type == 'o'){
                /* The root node becomes a leaf node */
                memcpy(data, &new_type, sizeof(char));
            }
            int new_entries_number = d;
            memcpy(data+sizeof(char), &new_entries_number, sizeof(int));

            BF_Block *new_block_leaf;
            BF_Block_Init(&new_block_leaf);
            char *sata;

            int new_leaf_id;
            if(BF_GetBlockCounter(Files_array[fileIndex].fileDesc, &new_leaf_id) != BF_OK){
                AM_errno = AME_BLOCKS;
                AM_PrintError("Error while getting the number of blocks.");
                exit(AM_errno);
            }
            /* Set the next_leaf 'pointer' of the first leaf node to 'point' to the new leaf node. */
            memcpy(data+sizeof(char)+sizeof(int), &new_leaf_id, sizeof(int));

            if(BF_AllocateBlock(Files_array[fileIndex].fileDesc, new_block_leaf) != BF_OK){
                AM_errno = AME_ALLOCATE;
                AM_PrintError("Error while allocating a new block.");
                exit(AM_errno);
            }
            sata = BF_Block_GetData(new_leaf_block);
            memcpy(sata, &new_type, sizeof(char));
            memcpy(sata+sizeof(char), &new_entries_number, sizeof(int));
            memcpy(sata+sizeof(char)+sizeof(int), &next_leaf_id, sizeof(int));
            memcpy(sata+sizeof(char)+sizeof(int)*2, &nodePointer, sizeof(int));

            for(int i = 0; i < d; i++){
                int new_entry_position = leaf_offset + sizeof(int)*max_entries + i*entry_size;
                memcpy(sata+leaf_offset+sizeof(int)*i, &new_entry_position, sizeof(int));

                int previous_entry_position = leaf_offset + sizeof(int)*max_entries + (d+i)*entry_size;
                memcpy(sata+new_entry_position, data+previous_entry_position, entry_size);
            }

            void *value = malloc(Files_array[fileIndex].attrLength1);

            BF_Block_SetDirty(block);
            BF_Block_SetDirty(new_block_leaf);
            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                AM_PrintError("Error while unpinning a block.");
                exit(AM_errno);
            }
            if(BF_UnpinBlock(new_block_leaf) != BF_OK){
                AM_errno = AME_UNPIN;
                AM_PrintError("Error while unpinning a block.");
                exit(AM_errno);
            }

            int result = insertEntry(fileIndex, , value1, value2, newchildentry);

            if(result != 1){
                AM_errno = AME_INSERT_ERROR;
                AM_PrintError("Error while inserting an entry.");
                exit(AM_errno);
            }


            if(Files_array[fileIndex].attrType1 == 'i'){
                int entries_array[entries+1];

                for(int i = 0; i < entries; i++){
                    //memcpy(&entries_array[i], data+leaf_offset+entry_size*i, sizeof(int));
                }
            }
        } else {
            /* L has space, put entry on it, set newChildEntry to NULL and return */
            int entry_position = leaf_offset + max_entries*sizeof(int) + entries*entry_size;
            memcpy(data+entry_position, value1, Files_array[fileIndex].attrLength1);
            int second_field_position = entry_position+Files_array[fileIndex].attrLength1;
            memcpy(data+second_field_position, value2, Files_array[fileIndex].attrLength2);
            entries++;
            memcpy(data+sizeof(char), &entries, sizeof(int));

            int i = 0;
            int flag = -1;
            
            while(flag == -1 && i < entries-1){
                void *value = malloc(Files_array[fileIndex].attrLength1);
            
                int test_entry_position = leaf_offset + max_entries*sizeof(int) + entry_size*i;
                memcpy(value, data+test_entry_position, Files_array[fileIndex].attrLength1);

                if( *(int*)value1 < *(int*)value ){
                    flag = 0;
                    break;
                }
                i++;
            }

            if(flag == -1){
                /* Place the new entry_position to the last entry of the array */
                int entry_in_array_position = leaf_offset + (entries-1)*sizeof(int);
                memcpy(data+entry_in_array_position, &entry_position, sizeof(int));
            } else {
                /* Place the new entry_position so that the entries of the array remain in ascending order:
                 *  Move all the entries of the array one position starting from the position that the new entry_position needs to be keeped.
                 *  Place the new entry_position to the array.
                 */
                int temp_this, temp_next;
                memcpy(&temp_this, data+leaf_offset+(i)*sizeof(int), sizeof(int));
                memcpy(&temp_next, data+leaf_offset+(i+1)*sizeof(int), sizeof(int));

                for(int j = i+1; j < entries; j++){
                    memcpy(data+leaf_offset+j*sizeof(int), &temp_this, sizeof(int));
                    temp_this = temp_next;
                    memcpy(&temp_next, data+leaf_offset+(j+1)*sizeof(int), sizeof(int));
                }
                memcpy(data+leaf_offset+i*sizeof(int), &entry_position, sizeof(int));
            }
            newChildEntry = NULL;
            BF_Block_SetDirty(block);
            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                AM_PrintError("Error while unpining a block.");
                exit(AM_errno);
            }
            return AME_OK;

        }
    }
/*
    if(type == 'l'){
        if(space-record_size >= 0){
            //Put entry in it, set newChildEntry to NULL and return.
        } else {
            //Split: First d entries stay, rest move to brand new node.
        }
    }

    if(type == 'n'){
        //Find i(position) that K(i) <= entry's key value < K(i+1)
        insert(fileIndex, position, value1, value2, newChildEntry);
        if(newChildEntry == NULL){
            return AME_OK;
        } else {
            //We spit child, must insert *newChildEntry in N
            if(space-Files_array[fileIndex].attrLength1 >= 0){
                //insert the newChildEntry on it, set newChildEntry to NULL and return.
                *newChildEntry = NULL;
                return AME_OK;
            } else {
                //Split N: first d key values and d+1 nodepointers stay, last d keys and d+1 nodepointers move to new node

                if(position == Files_array[fileIndex].rootBlock){
                    //create new node with <pointer to N, *newChildEntry>
                    //make the tree's root node pointer point to the new node.
                    return AME_OK;
                }
            }
        }
    }
    */
    return AME_OK;
}

/**
 * AM_OpenIndexScan(int fileDesc, int op, void *value)
 *  returns: AME_OK - if it succeeds, Some other error code - if it fails for some reason.
 *
 * This function opens a scan(search) of the file that is defined by the parameter
 * fileDesc. This scan has the purpose to find the records whose values in the
 * key-field of the file satisfy the comparison operator op, with regards to the
 * value that is pointed by the parameter value.
 *
 *  The various comparison operators are codes as follows:
 *      [-] 1 EQUAL (key-field == value)
 *      [-] 2 NOT EQUAL (key-field != value)
 *      [-] 3 LESS THAN (key-field < value)
 *      [-] 4 GREATER THAN (key-field > value)
 *      [-] 5 LESS THAN or EQUAL (key-field <= value)
 *      [-] 6 GREATER THAN or EQUAL (key-field >= value)
 *
 * The function returns a non-negative integer that corrensponds to a position of
 * the Scan_array that is implemented and keeped updated in the memory in regards
 * to all the scans that are opened at each moment.
 */
int AM_OpenIndexScan(int fileDesc, int op, void *value) {
  return AME_OK;
}

/**
 * AM_FindNextEntry(int scanDesc)
 *  returns: AME_OK - if it succeeds, NULL - and it sets AM_errno to a value.
 *
 * This function returns the value of the second field of the next entry that
 * satisfies the condition that is defined by the scan that corrensponds to
 * scanDesc. If there are no more records if returns NULL and sets the global
 * variable AM_errno to AME_EOF.
 */
void *AM_FindNextEntry(int scanDesc) {
	
}

/**
 * AM_CloseIndexScan(int scanDesc)
 *  returns: AME_OK - if it succeeds, AME_CLOSE_SCAN_ERROR - if it fails.
 *
 * This function terminates the scan of a file and removes the corrensponding registry
 * from the table of open scans.
 */
int AM_CloseIndexScan(int scanDesc) {
  return AME_OK;
}

/**
 * AM_PrintError(char *errString)
 *  returns: void
 *
 * This function prints the text that is pointed by errString and then prints the message
 * that corrensponds to the last error that derived by any of the AM functions. For this
 * purpose, this function uses a global variable AM_errno which is correctly updated in all
 * the rest functions.
 */
void AM_PrintError(char *errString) {
    printf("%s\n", errString);

    switch(AM_errno) {
        case AME_INIT: 
                printf("The BF level could not be initialized.");
                break;
        case AME_CREATE_FILE: 
                printf("The file could not be created.");
                break;
        case AME_OPEN_FILE:
                printf("The file could not be opened.");
                break;
        case AME_ALLOCATE:
                printf("The block could not be allocated.");
                break;
        case AME_COUNTER:
                printf("The number of blocks could not be obtained.");
                break;
        case AME_BLOCKS:
                printf("The number of blocks in the file is invalid.");
                break;
        case AME_GETBLOCK:
                printf("The block could not be obtained.");
                break;
        case AME_UNPIN:
                printf("The block could not be unpined.");
                break;
        case AME_DESTROY:
                printf("The file exists in the opened files array.");
                break;
        case AME_REMOVE:
                printf("The file could not be removed.");
                break;
        case AME_OPENINDEX:
                printf("There are already MAX_OPENED_FILES opened.");
                break;
        case AME_OPEN_SCAN:
                printf("There is an opened scan for that file.");
                break;
        case AME_CLOSE:
                printf("The file could not be closed.");
                break;
        case AME_CLOSE_NOT_EXIST:
                printf("There is no such opened file in the Files_array.");
                break;
        case AME_INSERT_ERROR:
                printf("The entry could not be inserted.");
                break;
        case AME_FILE_DESC_NOT_FOUND:
                printf("The file could not be found in the Files_array.");
                break;

    }
}

/**
 * AM_Close()
 *  returns: void
 *
 * This function is used to destroy all the structures that have been initialized.
 */
void AM_Close() {
  
}
