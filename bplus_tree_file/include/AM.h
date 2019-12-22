#ifndef AM_H_
#define AM_H_

/* Error codes */

extern int AM_errno;

#define AME_OK 0
#define AME_INIT 1
#define AME_CREATE_FILE 2
#define AME_OPEN_FILE 3
#define AME_ALLOCATE 4
#define AME_COUNTER 5
#define AME_BLOCKS 6
#define AME_GETBLOCK 7
#define AME_UNPIN 8
#define AME_DESTROY 9
#define AME_REMOVE 10
#define AME_OPENINDEX 11
#define AME_OPEN_SCAN 12
#define AME_CLOSE 13
#define AME_CLOSE_NOT_EXIST 14
#define AME_INSERT_ERROR 15
#define AME_FILE_DESC_NOT_FOUND 16
#define AME_ERROR 17
#define AME_TYPE 18
#define AME_MAXSCANS 19
#define AME_NOTOPEN 20
#define AME_EOF -1

/* Defines for array sizes */
#define MAX_OPEN_FILES 20
#define MAX_OPEN_SCANS 20

#define EQUAL 1
#define NOT_EQUAL 2
#define LESS_THAN 3
#define GREATER_THAN 4
#define LESS_THAN_OR_EQUAL 5
#define GREATER_THAN_OR_EQUAL 6

void AM_Init( void );


int AM_CreateIndex(
  char *fileName, /* όνομα αρχείου */
  char attrType1, /* τύπος πρώτου πεδίου: 'c' (συμβολοσειρά), 'i' (ακέραιος), 'f' (πραγματικός) */
  int attrLength1, /* μήκος πρώτου πεδίου: 4 γιά 'i' ή 'f', 1-255 γιά 'c' */
  char attrType2, /* τύπος πρώτου πεδίου: 'c' (συμβολοσειρά), 'i' (ακέραιος), 'f' (πραγματικός) */
  int attrLength2 /* μήκος δεύτερου πεδίου: 4 γιά 'i' ή 'f', 1-255 γιά 'c' */
);


int AM_DestroyIndex(
  char *fileName /* όνομα αρχείου */
);


int AM_OpenIndex (
  char *fileName /* όνομα αρχείου */
);


int AM_CloseIndex (
  int fileDesc /* αριθμός που αντιστοιχεί στο ανοιχτό αρχείο */
);


int AM_InsertEntry(
  int fileDesc, /* αριθμός που αντιστοιχεί στο ανοιχτό αρχείο */
  void *value1, /* τιμή του πεδίου-κλειδιού προς εισαγωγή */
  void *value2 /* τιμή του δεύτερου πεδίου της εγγραφής προς εισαγωγή */
);

int insertEntry(int fileDesc, int nodePointer, void *value1, void *value2, void* newChildEntry);

int AM_OpenIndexScan(
  int fileDesc, /* αριθμός που αντιστοιχεί στο ανοιχτό αρχείο */
  int op, /* τελεστής σύγκρισης */
  void *value /* τιμή του πεδίου-κλειδιού προς σύγκριση */
);

int search(int fileIndex, void *value, int nodePointer);

void *AM_FindNextEntry(
  int scanDesc /* αριθμός που αντιστοιχεί στην ανοιχτή σάρωση */
);


int AM_CloseIndexScan(
  int scanDesc /* αριθμός που αντιστοιχεί στην ανοιχτή σάρωση */
);


void AM_PrintError(
  char *errString /* κείμενο για εκτύπωση */
);

void AM_Close();


#endif /* AM_H_ */
