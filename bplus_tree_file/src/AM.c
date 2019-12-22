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
    void *value;
    int operator;
    int block;
    int position;
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
        AM_PrintError("Error while initializing the file.");
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
        Scans_array[i].operator = 0;
        Scans_array[i].value = NULL;
        Scans_array[i].block = -1;
        Scans_array[i].position = -1;
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
    if((attrType1 == 'i' && attrLength1 != sizeof(int)) || (attrType1 == 'f' && attrLength1 != sizeof(float)) || (attrType1 == 'c' && (attrLength1 < 1 || attrLength1 > 255))){
        AM_errno = AME_TYPE;
        return AM_errno;
    }

    if((attrType2 == 'i' && attrLength2 != sizeof(int)) || (attrType2 == 'f' && attrLength2 != sizeof(float)) || (attrType2 == 'c' && (attrLength2 < 1 || attrLength2 > 255))){
        AM_errno = AME_TYPE;
        return AM_errno;
    }

    if (BF_CreateFile(fileName) != BF_OK){
        AM_errno = AME_CREATE_FILE;
        return AM_errno;
    }
    
    int fileDesc;
    if(BF_OpenFile(fileName, &fileDesc) != BF_OK){
        AM_errno = AME_OPEN_FILE;
        return AM_errno;
    }

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;
    if(BF_AllocateBlock(fileDesc, block) != BF_OK){
        AM_errno = AME_ALLOCATE;
        return AM_errno;
    }

    /* Make sure that there is only one block allocated in the file. */
    int blocks_num;
    if(BF_GetBlockCounter(fileDesc, &blocks_num) != BF_OK){
        AM_errno = AME_COUNTER;
        return AM_errno;
    }

    if(blocks_num != 1){
        AM_errno = AME_BLOCKS;
        return AM_errno;
    }

    if(BF_GetBlock(fileDesc, 0, block) != BF_OK){
        AM_errno = AME_GETBLOCK;
        return AM_errno;
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
        return AM_errno;
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
            return AM_errno;
        }
    }

    if(remove(fileName) == 0) {
        printf("File removed successfully.");
        return AME_OK;
    } else {
        AM_errno = AME_REMOVE;
        return AM_errno;
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
            /* An empty position in the Files_array has been found */
            position = i;
            Files_array[i].fileName = (char*)malloc(sizeof(char)*strlen(fileName));
            strcpy(Files_array[i].fileName, fileName);

            int fileDesc;
            if(BF_OpenFile(fileName, &fileDesc) != BF_OK){
                AM_errno = AME_OPEN_FILE;
                return AM_errno;
            }
            Files_array[i].fileDesc = fileDesc;

            BF_Block *block;
            BF_Block_Init(&block);
            char *data;
            
            if(BF_GetBlock(fileDesc, 0, block) != BF_OK){
                AM_errno = AME_GETBLOCK;
                return AM_errno;
            }
            
            data = BF_Block_GetData(block);
            int root;
            char attrType1, attrType2;
            int attrLength1, attrLength2;
            memcpy(&root, data+sizeof(char)*3+sizeof(int)*2, sizeof(int));
            memcpy(&attrType1, data+sizeof(char), sizeof(char));
            memcpy(&attrLength1, data+sizeof(char)*2, sizeof(int));
            memcpy(&attrType2, data+sizeof(char)*2+sizeof(int), sizeof(char));
            memcpy(&attrLength2, data+sizeof(char)*3+sizeof(int), sizeof(int));

            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                return AM_errno;
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
        return AM_errno;
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
            return AM_errno;
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
            return AM_errno;
        }
    }else{
        AM_errno = AME_CLOSE_NOT_EXIST;
        return AM_errno;
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
    /* The nodes inside the file can be categorized in 4 ways, which will be represented by a (char) inside the file. 
     *  ['o'] this means that the node is a root node and a leaf.
     *  ['r'] this means that the node is a root node but not a leaf.
     *  ['n'] this means that the node is an internal tree node.
     *  ['l'] this means that the node is a leaf node.
     *
     * The fileDesc value is the position of the opened file in the array. So it is like an index. */
    int root = Files_array[fileDesc].rootBlock;
    int attrLength1 = Files_array[fileDesc].attrLength1, attrLength2 = Files_array[fileDesc].attrLength2;
    char attrType1 = Files_array[fileDesc].attrType1, attrType2 = Files_array[fileDesc].attrType2;
    int file_id = Files_array[fileDesc].fileDesc;

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;

    int blocks_number;
    if(BF_GetBlockCounter(file_id, &blocks_number) != BF_OK){
        AM_errno = AME_BLOCKS;
        return AM_errno;
    }
    if(blocks_number == 1){
        /*  In this case, this is the first entry inserted in the file.
         *  We need to allocate and initiallize a new block, which will be a root as well as a leaf node.
         *  Next we insert the first entry.
         */
        if(BF_AllocateBlock(file_id, block) != BF_OK){
            AM_errno = AME_ALLOCATE;
            return AM_errno;
        }

        char type = 'o';
        int entries = 1;
        int next_leaf = -1;
        int prev_leaf = -1;

        /* Each leaf block holds the following information in the same order:
         * [ type - the type that shows if the block is a leaf
         *   entries - the number of entries that the block holds
         *   next_leaf - the next leaf block that holds bigger values
         *   prev_leaf - the previous leaf block that holds lower values
         *
         *   int max_entries[] - an array that holds the position/offset of the entries in ascending order.
         *   <entry1, entry2> - the entries themselves for each entry that is in the block in insertion order.
         * ]
         */
        int max_entries = (BF_BLOCK_SIZE - leaf_offset)/(attrLength1 + attrLength2 + sizeof(int));
        if(max_entries%2 == 1){
            max_entries--;
        }

        data = BF_Block_GetData(block);

        memcpy(data, &type, sizeof(char));
        memcpy(data+sizeof(char), &entries, sizeof(int));
        memcpy(data+sizeof(char)+sizeof(int), &next_leaf, sizeof(int));
        memcpy(data+sizeof(char)+sizeof(int)*2, &prev_leaf, sizeof(int));

        int entry_offset = -1; 
        for(int i = 0; i < max_entries; i++){
            memcpy(data+leaf_offset+i*sizeof(int), &entry_offset, sizeof(int));
        }

        memcpy(data+leaf_offset+max_entries*sizeof(int), value1, attrLength1);
        memcpy(data+leaf_offset+max_entries*sizeof(int)+attrLength1, value2, attrLength2);

        entry_offset = leaf_offset+max_entries*sizeof(int);
        memcpy(data+leaf_offset, &entry_offset, sizeof(int));

        BF_Block_SetDirty(block);
        if(BF_UnpinBlock(block) != BF_OK){
            AM_errno = AME_UNPIN;
            return AM_errno;
        }
    } else {
        /* If the B+Tree has already been built, then use a recursive insertEntry */
        void *newchildentry = NULL;
        int result = insertEntry(fileDesc, root, value1, value2, newchildentry);
        if(result != 0){
            AM_errno = AME_INSERT_ERROR;
            return AM_errno;
        }
    }
    BF_Block_Destroy(&block);
    return AME_OK;
}

/**
 * insertEntry(int fileDesc, int nodePointer, void *value1, void *value2, void *newchildentry)
 *  returns: 0 - if it runs correctly, Some error code - if it has an error.
 *
 *  This is the recursive insert entry that follows the path from the root down to the leaf that the entry needs to be placed.
 *  Then it inserts the entry if there is space, or it splits the leaf into two leafs with equal number of entries, and recursively sends 
 *  the key of the entry to be inserted to the parent node(which may need to be splitted).
 *
 *  fileDesc - holds the index of the Files_array in which the file that the insert will take place is.
 *  nodePointer - holds the number of the node/block that the insertion will take place.
 *  newchildentry - is NULL except for the case that there was a split, and it holds the pair <key-value, block-number> that will be inserted to the node parent.
 *                  key-value is the first value of the new block that was created due to the split.
 *                  block-number is the number of the new block that holds the splitted entries.
 */
int insertEntry(int fileDesc, int nodePointer, void *value1, void *value2, void *newchildentry){
    int root = Files_array[fileDesc].rootBlock;
    int attrLength1 = Files_array[fileDesc].attrLength1, attrLength2 = Files_array[fileDesc].attrLength2;
    char attrType1 = Files_array[fileDesc].attrType1, attrType2 = Files_array[fileDesc].attrType2;
    int file_id = Files_array[fileDesc].fileDesc;

    int blocks_num;
    int entry_size = attrLength1+attrLength2;
    int node_entry_size = sizeof(int)+attrLength1;
    int max_entries = (BF_BLOCK_SIZE-leaf_offset)/(attrLength1+attrLength2+sizeof(int));
    if(max_entries%2 == 1){
        max_entries--;
    }
    int d = max_entries/2;

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;

    if(BF_GetBlock(file_id, nodePointer, block) != BF_OK){
        AM_errno = AME_GETBLOCK;
        return AM_errno;
    }
    data =  BF_Block_GetData(block);

    char type;
    int entries;
    memcpy(&type, data, sizeof(char));
    memcpy(&entries, data+sizeof(char), sizeof(int));

    if(type == 'o' || type == 'l'){
        /* In this case the block is always a leaf node, either a root or a plain leaf */
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
            memcpy(data+sizeof(char), &d, sizeof(int));

            BF_Block *new_block_leaf;
            BF_Block_Init(&new_block_leaf);
            char *sata;

            int new_leaf_id;
            if(BF_GetBlockCounter(file_id, &new_leaf_id) != BF_OK){
                AM_errno = AME_BLOCKS;
                return AM_errno;
            }
            /* Set the next_leaf 'pointer' of the first leaf node to 'point' to the new leaf node. */
            memcpy(data+sizeof(char)+sizeof(int), &new_leaf_id, sizeof(int));

            if(BF_AllocateBlock(file_id, new_block_leaf) != BF_OK){
                AM_errno = AME_ALLOCATE;
                return AM_errno;
            }
            sata = BF_Block_GetData(new_block_leaf);

            memcpy(sata, &new_type, sizeof(char));
            memcpy(sata+sizeof(char), &d, sizeof(int));
            memcpy(sata+sizeof(char)+sizeof(int), &next_leaf_id, sizeof(int));
            memcpy(sata+sizeof(char)+sizeof(int)*2, &nodePointer, sizeof(int));

            for(int i = 0; i < d; i++){
                int new_entry_position = leaf_offset + sizeof(int)*max_entries + i*entry_size;
                memcpy(sata+leaf_offset+sizeof(int)*i, &new_entry_position, sizeof(int));

                int previous_entry_position = leaf_offset + sizeof(int)*max_entries + (d+i)*entry_size;
                memcpy(sata+new_entry_position, data+previous_entry_position, entry_size);
            }

            void *value = malloc(attrLength1);
            int first_entry_position = leaf_offset + sizeof(int)*max_entries;
            memcpy(value, sata+first_entry_position, attrLength1);

            int node;
            if(attrType1 == 'i'){
                if ( *(int*)value1 < *(int*)value ){
                    node = nodePointer;
                } else {
                    node = new_leaf_id;
                }
            } else if(attrType1 == 'f'){
                if ( *(float*)value1 < *(float*)value ){
                    node = nodePointer;
                } else {
                    node = new_leaf_id;
                }
            } else if(attrType1 == 'c'){
                if ( strcmp(value1, value) < 0){
                    node = nodePointer;
                } else {
                    node = new_leaf_id;
                }
            } else{
                AM_errno = AME_ERROR;
                return AM_errno;
            }

               
            BF_Block_SetDirty(block);
            BF_Block_SetDirty(new_block_leaf);
            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                return AM_errno;
            }
            if(BF_UnpinBlock(new_block_leaf) != BF_OK){
                AM_errno = AME_UNPIN;
                return AM_errno;
            }

            int result = insertEntry(fileDesc, node, value1, value2, newchildentry);
            if(result != 1){
                AM_errno = AME_INSERT_ERROR;
                return AM_errno;
            }
            
            free(value);

            if(type == 'o'){
                /* The leaf-node was also a root node, so a new root node must be made, this time it will never be a leaf-node again */
                int new_root_block;
                if(BF_GetBlockCounter(file_id, &new_root_block) != BF_OK){
                    AM_errno = AME_BLOCKS;
                    return AM_errno;
                }
                int entries = 1;
                if(BF_AllocateBlock(file_id, block) != BF_OK){
                    AM_errno = AME_ALLOCATE;
                    return AM_errno;
                }
                data = BF_Block_GetData(block);
                memcpy(data, &type, sizeof(char));
                memcpy(data+sizeof(char), &entries, sizeof(int));
                memcpy(data+node_offset, &nodePointer, sizeof(int));
                memcpy(data+node_offset+sizeof(int), value1, attrLength1);
                memcpy(data+node_offset+sizeof(int)+attrLength1, &new_leaf_id, sizeof(int));
                BF_Block_SetDirty(block);
                if(BF_UnpinBlock(block) != BF_OK){
                    AM_errno = AME_UNPIN;
                    return AM_errno;
                }
                Files_array[fileDesc].rootBlock = new_root_block;
                if(BF_GetBlock(file_id, 0, block) != BF_OK){
                    AM_errno = AME_GETBLOCK;
                    return AM_errno;
                }
                data = BF_Block_GetData(block);
                memcpy(data+sizeof(char)*3+sizeof(int)*2, &new_root_block, sizeof(int));
                BF_Block_SetDirty(block);
                if(BF_UnpinBlock(block) != BF_OK){
                    AM_errno = AME_UNPIN;
                    return AM_errno;
                }

            }

            newchildentry = malloc(sizeof(int)+attrLength1);
            if(BF_GetBlock(file_id, new_leaf_id, block) != BF_OK){
                AM_errno = AME_GETBLOCK;
                return AM_errno;
            }

            data = BF_Block_GetData(block);
            memcpy(&first_entry_position, data+leaf_offset, sizeof(int));
            memcpy(newchildentry, data+first_entry_position, attrLength1);
            memcpy(newchildentry+attrType1, &new_leaf_id, sizeof(int));
            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                return AM_errno;
            }
           return 0;
        } else {
            /* L has space, put entry on it, set newChildEntry to NULL and return
             * The entry will be placed after the last entry of the leaf node, and the array with the positions of the ascending order of the entries will be updated.
             */
            int entry_position = leaf_offset + max_entries*sizeof(int) + entries*entry_size;
            memcpy(data+entry_position, value1, attrLength1);
            int second_field_position = entry_position+attrLength1;
            memcpy(data+second_field_position, value2, attrLength2);
            entries++;
            memcpy(data+sizeof(char), &entries, sizeof(int));

            int i = 0;
            int flag = -1;
            void *value = malloc(attrLength1);
            
            while(flag == -1 && i < entries-1){
                /* Find the position (i) of the array which holds the value that is bigger than the new value inserted. */
                int test_entry_position = leaf_offset + max_entries*sizeof(int) + entry_size*i;
                memcpy(value, data+test_entry_position, attrLength1);
                if(attrType1 == 'i'){
                    if( *(int*)value1 < *(int*)value ){
                        flag = 0;
                        break;
                    }
                } else if(attrType1 == 'f'){
                    if( *(float*)value1 < *(float*)value ){
                        flag = 0;
                        break;
                    }

                } else if(attrType1 == 'c'){
                    if( strcmp(value1, value) < 0){
                        flag = 0;
                        break;
                    }
                } else{
                    AM_errno = AME_ERROR;
                    return AM_errno;
                }


                i++;
            }

            if(flag == -1){
                /* The new value inserted is the biggest of the entries in the leaf node.
                 * Place the new entry_position to the last entry of the array */
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
            free(newchildentry);
            newchildentry = NULL;
            BF_Block_SetDirty(block);
            if(BF_UnpinBlock(block) != BF_OK){
                AM_errno = AME_UNPIN;
                return AM_errno;
            }
            return 0;
        }
    } else if ( type == 'r' || type == 'n' ){
        /* In this case the node was an internal node, either a root or a plain node. 
         * We need to find the leaf node in which the new value has to be inserted.
         */
        int next_node;
        for(int i = 0; i < entries; i++){
            void *left_value = malloc(attrLength1);
            void *right_value = malloc(attrLength1);
            int node_entry_size = sizeof(int)+attrLength1;

            memcpy(&next_node, data+node_offset+entry_size*i, sizeof(int));
            memcpy(left_value, data+node_offset+sizeof(int)+node_entry_size*i, attrLength1);
            memcpy(right_value, data+node_offset+sizeof(int)+node_entry_size*(i+1), attrLength1);
            if(attrType1 == 'i'){
                if( (*(int*)left_value <= *(int*)value1) && (*(int*)right_value > *(int*)value1)){
                    /* If the value is between the left_value and the right_value then we have found our node */
                    break;
                }
            } else if(attrType1 == 'f'){
                if( (*(float*)left_value <= *(float*)value1) && (*(float*)right_value > *(float*)value1)){
                    /* If the value is between the left_value and the right_value then we have found our node */
                    break;
                }
            } else if(attrType1 == 'c'){
                if( (strcmp(left_value, value1) <= 0) && (strcmp(right_value, value1) > 0)){
                    /* If the value is between the left_value and the right_value then we have found our node */
                    break;
                }
            } else{
                AM_errno = AME_ERROR;
                return AM_errno;
            }
            
        }
        if(next_node == 0){
            AM_errno = AME_ERROR;
            return AM_errno;
        }
       
        if(newchildentry == NULL){
            /* If the newchildentry is NULL then the insertion was finished without splitting
             * and we need to go down to insert to the next node.*/
            int result = insertEntry(fileDesc, next_node, value1, value2, newchildentry);
            if(result != 1){
                AM_errno = AME_INSERT_ERROR;
                return AM_errno;
            }
            return 0;
        } else {
            /* Else the previous insertion splitted the node and the newchildentry holds the following:
             * <key-value, block_number> : key-value is the lower value of the new block that was created: block_number is the block number that holds that value.
             * The new entry that has to be inserted in the internal node is the pair <key-value, block_number> so that the entries in the node are keeped in ascending order.
             */
            if(entries == max_entries){
                /* Split N: 2d+1 key values and 2d+2 nodepointers.
                 *  First d key values and d+1 pointers stay.
                 *  Last d keys and d+1 pointers move to new node N2
                 * Then we add the newchildentry to the corrensponding node.
                 */
                BF_Block *new_block_node;
                BF_Block_Init(&new_block_node);
                char *sata;
                
                void *value = malloc(attrLength1);
                void *new_value = malloc(attrLength1);

                memcpy(data+sizeof(char), &d, sizeof(int));

                int new_node_id;
                if(BF_GetBlockCounter(file_id, &new_node_id) != BF_OK){
                    AM_errno = AME_BLOCKS;
                    return AM_errno;
                }
                if(BF_AllocateBlock(file_id, new_block_node) != BF_OK){
                    AM_errno = AME_BLOCKS;
                    return AM_errno;
                }
                char new_type = 'n';
                sata = BF_Block_GetData(new_block_node);
                memcpy(sata, &new_type, sizeof(char));
                memcpy(sata+sizeof(char), &d, sizeof(int));
                int first_pointer;
                memcpy(&first_pointer, data+node_offset+node_entry_size*d, sizeof(int));
                memcpy(sata+node_offset, &first_pointer, sizeof(int));

                for(int i = 0; i < d; i++){
                    memcpy(sata+node_offset+sizeof(int)+node_entry_size*i, data+node_offset+sizeof(int)+node_entry_size*(d+i), node_entry_size);
                }

                memcpy(value, sata+node_offset+sizeof(int), attrLength1);
                memcpy(new_value, newchildentry, attrLength1);

                int node;
                if( *(int*)new_value < *(int*)value){
                    node = nodePointer;
                } else {
                    node = new_node_id;
                }

                int result = insertEntry(fileDesc, node, value1, value2, newchildentry);
                if(result != 0){
                    AM_errno = AME_INSERT_ERROR;
                    return AM_errno;
                }

                BF_Block_SetDirty(block);
                BF_Block_SetDirty(new_block_node);
                if(BF_UnpinBlock(block) != BF_OK){
                    AM_errno = AME_UNPIN;
                    return AM_errno;
                }
                if(BF_UnpinBlock(new_block_node) != BF_OK){
                    AM_errno = AME_UNPIN;
                    return AM_errno;
                }

                if(type == 'r'){
                    /* Root was split */
                    int root_block_number;
                    if(BF_GetBlockCounter(file_id, &root_block_number) != BF_OK){
                        AM_errno = AME_BLOCKS;
                        return AM_errno;
                    }
                    if(BF_AllocateBlock(file_id, block) != BF_OK){
                        AM_errno = AME_ALLOCATE;
                        return AM_errno;
                    }
                    int entries = 1;
                    data = BF_Block_GetData(block);
                    memcpy(data, &type, sizeof(char));
                    memcpy(data+sizeof(char), &entries, sizeof(int));
                    memcpy(data+node_offset, &nodePointer, sizeof(int));
                    memcpy(data+node_offset+sizeof(int), newchildentry, attrLength1);
                    memcpy(data+node_offset+sizeof(int)+attrLength1, &new_node_id, sizeof(int));
                    BF_Block_SetDirty(block);
                    if(BF_UnpinBlock(block) != BF_OK){
                        AM_errno = AME_UNPIN;
                        return AM_errno;
                    }
                    Files_array[fileDesc].rootBlock = root_block_number;

                    if(BF_GetBlock(file_id, 0, block) != BF_OK){
                        AM_errno = AME_UNPIN;
                        return AM_errno;
                    }
                    data = BF_Block_GetData(block);
                    memcpy(data+sizeof(char)*3+sizeof(int)*2, &root_block_number, sizeof(int));
                    BF_Block_SetDirty(block);
                    if(BF_UnpinBlock(block) != BF_OK){
                        AM_errno = AME_UNPIN;
                        return AM_errno;
                    }
                }
                return 0;
            } else {
                /* The internal node has space for the newchildentry to be added */
                int left_block;
                int right_block;
                void *value = malloc(attrLength1);
                void *child_value = malloc(attrLength1);

                memcpy(child_value, newchildentry, attrLength1);

                int position;
                for(int i = 0 ; i < entries ; i++){
                    memcpy(&left_block, data+node_offset+node_entry_size*i, sizeof(int));
                    memcpy(value, data+node_offset+sizeof(int)+node_entry_size*i, attrLength1);
                    memcpy(&right_block, data+node_offset+node_entry_size*i+sizeof(int)+attrLength1, sizeof(int));

                    if(attrType1 == 'i'){
                        if(*(int*)value > *(int*)child_value){
                            break;
                        }

                    } else if (attrType1 == 'f'){
                        if(*(float*)value > *(float*)child_value){
                            break;
                        }

                    } else if (attrType1 == 'c'){
                        if(strcmp(value, child_value) > 0){
                            break;
                        }
                    } else{
                        AM_errno = AME_ERROR;
                        return AM_errno;
                    }
                    position = i+1;
                }

                void *this_entry = malloc(node_entry_size);
                void *next_entry = malloc(node_entry_size);
                memcpy(this_entry, data+node_offset+sizeof(int)+node_entry_size*position, node_entry_size);
                memcpy(next_entry, data+node_offset+sizeof(int)+node_entry_size*(position+1), node_entry_size);

                for(int j = position+1; j < entries; j++){
                    memcpy(data+node_offset+sizeof(int)+node_entry_size*j, this_entry, node_entry_size);
                    this_entry = next_entry;
                    memcpy(next_entry, data+node_offset+sizeof(int)+node_entry_size*(j+1), node_entry_size);
                }

                memcpy(data+node_offset+sizeof(int)+node_entry_size*position, newchildentry, node_entry_size);
                BF_Block_SetDirty(block);
                if(BF_UnpinBlock(block) != BF_OK){
                    AM_errno = AME_UNPIN;
                    return AM_errno;
                }
                free(newchildentry);
                newchildentry = NULL;
                return 0;
            }
        }
    }
    return 0;
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
    int fileIndex = -1;
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(Files_array[i].fileDesc == fileDesc){
            fileIndex = i;
            break;
        }
    }
    if(fileIndex == -1){
        AM_errno = AME_NOTOPEN;
        return AM_errno;
    }

    int rootBlock = Files_array[fileIndex].rootBlock;
    int attrLength1 = Files_array[fileIndex].attrLength1, attrLength2 = Files_array[fileIndex].attrLength2;
    char attrType1 = Files_array[fileIndex].attrType1, attrType2 = Files_array[fileIndex].attrType2;

    int flag = -1;
    int i;
    for( i = 0; i < MAX_OPEN_SCANS; i++){
        if(Scans_array[i].value == NULL){
            flag = 0;
            break;
        }
    }
    if(flag == -1){
        AM_errno = AME_MAXSCANS;
        return AM_errno;
    }
    Scans_array[i].value = malloc(attrLength1);
    memcpy(Scans_array[i].value, value, attrLength1);
    Scans_array[i].operator = op;
    Scans_array[i].fileDesc = fileDesc;

    /* Find the block and the position of the entry that satisfies the condition of the operator
     * For that purpose find the block that could hold the value. It will be the starting block.
     */

    int leaf_block = search(fileIndex, value, rootBlock);
    Scans_array[i].block = leaf_block;
    Scans_array[i].position = -1;

    return AME_OK;
}

/**
 * Given a search key value, finds its leaf node.
 */
int search(int fileIndex, void *value, int nodePointer){
    int fileDesc = Files_array[fileIndex].fileDesc;
    char attrType1 = Files_array[fileIndex].attrType1;
    int attrLength1 = Files_array[fileIndex].attrLength1;
    char attrType2 = Files_array[fileIndex].attrType2;
    int attrLength2 = Files_array[fileIndex].attrLength2;

    int node_entry_size = attrLength1+sizeof(int);

    BF_Block *block;
    BF_Block_Init(&block);
    char *data;
    
    if(BF_GetBlock(fileDesc, nodePointer, block) != BF_OK){
        AM_errno = AME_GETBLOCK;
        return AM_errno;
    }
    data = BF_Block_GetData(block);

    char node_type;
    int entries;

    memcpy(&node_type, data, sizeof(char));
    memcpy(&entries, data+sizeof(char), sizeof(int));

    int left_value, right_value;
    void *first_key_value = malloc(attrLength1);
    void *last_key_value = malloc(attrLength1);


    if(node_type == 'o' || node_type == 'l'){
        if(BF_UnpinBlock(block) != BF_OK){
            AM_errno = AME_UNPIN;
            return AM_errno;
        }
        return nodePointer;
    } else if(node_type == 'r' || node_type == 'n'){
        if(attrType1 == 'i'){
            memcpy(&left_value, data+node_offset, sizeof(int));
            memcpy(first_key_value, data+node_offset+sizeof(int), attrLength1);
            
            if(*(int*)value < *(int*)first_key_value){
                return search(fileIndex, value, left_value);
            } else {
                memcpy(&right_value, data+node_offset+node_entry_size*entries, sizeof(int));
                memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*entries, attrLength1);

                if(*(int*)value > *(int*)last_key_value){
                    return search(fileIndex, value, right_value);
                } else {
                    int pointer;
                    for(int i = 1; i < entries; i++){
                        memcpy(&pointer, data+node_offset+node_entry_size*i, sizeof(int));
                        memcpy(first_key_value, data+node_offset+sizeof(int)+node_entry_size*(i-1), attrLength1);
                        memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*i, attrLength1);
                        
                        if((*(int*)value >= *(int*)first_key_value) &&(*(int*)value < *(int*)last_key_value)){
                            return search(fileIndex, value, pointer);
                        }
                    }
                    AM_errno = AME_ERROR;
                    return AM_errno;
                }
            }
        }
        if(attrType1 == 'f'){
            memcpy(&left_value, data+node_offset, sizeof(int));
            memcpy(first_key_value, data+node_offset+sizeof(int), attrLength1);
            
            if(*(float*)value < *(float*)first_key_value){
                return search(fileIndex, value, left_value);
            } else {
                memcpy(&right_value, data+node_offset+node_entry_size*entries, sizeof(int));
                memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*entries, attrLength1);

                if(*(float*)value > *(float*)last_key_value){
                    return search(fileIndex, value, right_value);
                } else {
                    int pointer;
                    for(int i = 1; i < entries; i++){
                        memcpy(&pointer, data+node_offset+node_entry_size*i, sizeof(int));
                        memcpy(first_key_value, data+node_offset+sizeof(int)+node_entry_size*(i-1), attrLength1);
                        memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*i, attrLength1);
                        
                        if((*(float*)value >= *(float*)first_key_value) &&(*(float*)value < *(float*)last_key_value)){
                            return search(fileIndex, value, pointer);
                        }
                    }
                    AM_errno = AME_ERROR;
                    return AM_errno;
                }
            }
        }
        if(attrType1 == 'c'){
            memcpy(&left_value, data+node_offset, sizeof(int));
            memcpy(first_key_value, data+node_offset+sizeof(int), attrLength1);
            
            if(strcmp(value, first_key_value) < 0){
                return search(fileIndex, value, left_value);
            } else {
                memcpy(&right_value, data+node_offset+node_entry_size*entries, sizeof(int));
                memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*entries, attrLength1);

                if(strcmp(value, last_key_value) > 0){
                    return search(fileIndex, value, right_value);
                } else {
                    int pointer;
                    for(int i = 1; i < entries; i++){
                        memcpy(&pointer, data+node_offset+node_entry_size*i, sizeof(int));
                        memcpy(first_key_value, data+node_offset+sizeof(int)+node_entry_size*(i-1), attrLength1);
                        memcpy(last_key_value, data+node_offset+sizeof(int)+node_entry_size*i, attrLength1);
                        
                        if((strcmp(value, first_key_value) >= 0) && (strcmp(value, last_key_value) < 0)){
                            return search(fileIndex, value, pointer);
                        }
                    }
                    AM_errno = AME_ERROR;
                    return AM_errno;
                }
            }
        }
        if(BF_UnpinBlock(block) != BF_OK){
            AM_errno = AME_UNPIN;
            return AM_errno;
        }
    } else {
        AM_errno = AME_ERROR;
        return AM_errno;
    }

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

    //scan_info[scanDesc] = NULL; // So we can recognize which elements of the array are not being used

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
                printf("The BF level could not be initialized.\n");
                break;
        case AME_CREATE_FILE: 
                printf("The file could not be created.\n");
                break;
        case AME_OPEN_FILE:
                printf("The file could not be opened.\n");
                break;
        case AME_ALLOCATE:
                printf("The block could not be allocated.\n");
                break;
        case AME_COUNTER:
                printf("The number of blocks could not be obtained.\n");
                break;
        case AME_BLOCKS:
                printf("The number of blocks in the file is invalid.\n");
                break;
        case AME_GETBLOCK:
                printf("The block could not be obtained.\n");
                break;
        case AME_UNPIN:
                printf("The block could not be unpined.\n");
                break;
        case AME_DESTROY:
                printf("The file exists in the opened files array.\n");
                break;
        case AME_REMOVE:
                printf("The file could not be removed.\n");
                break;
        case AME_OPENINDEX:
                printf("There are already MAX_OPENED_FILES opened.\n");
                break;
        case AME_OPEN_SCAN:
                printf("There is an opened scan for that file.\n");
                break;
        case AME_CLOSE:
                printf("The file could not be closed.\n");
                break;
        case AME_CLOSE_NOT_EXIST:
                printf("There is no such opened file in the Files_array.\n");
                break;
        case AME_INSERT_ERROR:
                printf("The entry could not be inserted.\n");
                break;
        case AME_FILE_DESC_NOT_FOUND:
                printf("The file could not be found in the Files_array.\n");
                break;
        case AME_ERROR:
                printf("Fatal error occured.\n");
                break;
        case AME_TYPE:
                printf("The attribute type and length do not add up.\n");
                break;
        case AME_MAXSCANS:
                printf("The Scans_array is already full.");
                break;
        case AME_NOTOPEN:
                printf("The file is not opened.");
                break;
        default:
                printf("No error was attributed.\n");
                break;

    }
    return;
}

/**
 * AM_Close()
 *  returns: void
 *
 * This function is used to destroy all the structures that have been initialized.
 */
void AM_Close() {
  
}
