// smart_inventory_with_donations.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#define MAX_NAME 50
#define MAX_PREF 200
#define HASH_SIZE 100
#define HEAP_CAPACITY 1000
#define CATEGORY_MAX 20
#define DATA_DIR "./data/"
#define DONATION_FILE "./data/donations.dat"

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define RESET "\x1b[0m"

// Typing animation speed (milliseconds per character)
#define TYPE_DELAY_MS 15  // Medium speed

// --- Portable sleep for ms ---
void msleep(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// Enable ANSI Colors on Windows consoles that support VT
void enableColors() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif
}

// Typing animation function
void typewrite(const char *text, int delay_ms) {
    const char *p = text;
    while (*p) {
        putchar(*p++);
        fflush(stdout);
        msleep((unsigned int)delay_ms);
    }
}

// --- Data Structures ---
typedef struct Item {
    int id;
    char name[MAX_NAME];
    int quantity;
    char expiry[11];
    float price;
    char supplier[MAX_NAME];
    char category[MAX_NAME];
    struct Item *next;
    struct Item *hash_next;
} Item;

typedef struct Organization {
    char name[MAX_NAME];
    char contact[MAX_NAME];
    char preferences[MAX_PREF];
    struct Organization *next;
} Organization;

typedef struct BSTNode {
    Item *item;
    struct BSTNode *left;
    struct BSTNode *right;
} BSTNode;

typedef struct MinHeap {
    Item **arr;
    int size;
    int capacity;
} MinHeap;

typedef struct HashTable {
    Item *table[HASH_SIZE];
} HashTable;

typedef struct ProductDB {
    int id;
    char name[MAX_NAME];
    char expiry[11];
    float price;
    char supplier[MAX_NAME];
    char category[MAX_NAME];
} ProductDB;

typedef struct DonationRecord {
    int itemId;
    char itemName[MAX_NAME];
    char donorName[MAX_NAME];
    char donorContact[MAX_NAME];
    int quantityDonated;
    char date[20];
    struct DonationRecord *next; // only for in-memory list usage
} DonationRecord;

// Product Database
ProductDB products[] = {
    {101, "Milk", "2025-11-20", 45.50, "DairyFarm", "Dairy"},
    {102, "Bread", "2025-11-25", 25.00, "BakeryHouse", "Bakery"},
    {103, "Cheese", "2026-01-15", 120.00, "CheeseCo", "Dairy"},
    {104, "Eggs", "2025-12-05", 60.00, "EggFarm", "Poultry"},
    {105, "Yogurt", "2025-11-17", 30.00, "DairyFarm", "Dairy"},
    {106, "Butter", "2025-12-10", 80.00, "DairyFarm", "Dairy"},
    {107, "Chocolate", "2026-01-20", 50.00, "SweetCo", "Snacks"},
    {108, "Apple", "2025-11-28", 10.00, "FruitFarm", "Fruits"},
    {109, "Orange Juice", "2025-12-15", 60.00, "JuiceCo", "Beverages"},
    {110, "Cereal", "2026-02-10", 120.00, "CerealCo", "Grains"},
    {111, "Tomato", "2025-11-22", 5.00, "VegFarm", "Vegetables"},
    {112, "Potato", "2025-12-05", 2.50, "VegFarm", "Vegetables"},
    {113, "Carrot", "2025-12-01", 4.00, "VegFarm", "Vegetables"},
    {114, "Paneer", "2025-12-12", 90.00, "DairyFarm", "Dairy"},
    {115, "Chicken", "2025-11-20", 200.00, "MeatCo", "Meat"},
    {116, "Fish", "2025-11-21", 250.00, "SeafoodCo", "Seafood"},
    {117, "Rice", "2026-03-10", 50.00, "GrainFarm", "Grains"},
    {118, "Pasta", "2026-01-30", 60.00, "PastaCo", "Grains"},
    {119, "Snacks", "2025-12-25", 40.00, "SnackCo", "Snacks"},
    {120, "Juice Box", "2025-11-27", 20.00, "JuiceCo", "Beverages"}
};
int productCount = sizeof(products) / sizeof(products[0]);

// --- Utility Functions ---
void ensureDataDirectory() {
    struct stat st = {0};
    if (stat(DATA_DIR, &st) == -1) {
#ifdef _WIN32
        _mkdir(DATA_DIR);
#else
        mkdir(DATA_DIR, 0777);
#endif
        printf(GREEN "Created data directory: %s" RESET "\n", DATA_DIR);
    }
}

void logAction(const char *action, int itemId, const char *details) {
    ensureDataDirectory();
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%sactivity.log", DATA_DIR);
    FILE *log = fopen(filepath, "a");
    if (log) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log, "[%s] %s | Item ID: %d | %s\n", timestamp, action, itemId, details);
        fclose(log);
    }
}

void getCurrentDate(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm today = *localtime(&now);
    strftime(buffer, size, "%Y-%m-%d", &today);
}

int isValidDate(const char *date) {
    int y, m, d;
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return 0;
    if (y < 2000 || y > 2100) return 0;
    if (m < 1 || m > 12) return 0;
    int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0))) {
        daysInMonth[2] = 29;
    }
    if (d < 1 || d > daysInMonth[m]) return 0;
    return 1;
}

int compareDate(const char *d1, const char *d2) {
    int y1, m1, day1, y2, m2, day2;
    sscanf(d1, "%d-%d-%d", &y1, &m1, &day1);
    sscanf(d2, "%d-%d-%d", &y2, &m2, &day2);
    if (y1 != y2) return y1 - y2;
    if (m1 != m2) return m1 - m2;
    return day1 - day2;
}

int daysUntilExpiry(const char *expiry) {
    time_t t = time(NULL);
    struct tm today = *localtime(&t);
    int y, m, d;
    sscanf(expiry, "%d-%d-%d", &y, &m, &d);
    struct tm exp = {0};
    exp.tm_year = y - 1900;
    exp.tm_mon = m - 1;
    exp.tm_mday = d;
    today.tm_hour = today.tm_min = today.tm_sec = 0;
    double diff = difftime(mktime(&exp), mktime(&today));
    return (int)(diff / (60 * 60 * 24));
}

int sameMonth(const char *expiry) {
    time_t t = time(NULL);
    struct tm today = *localtime(&t);
    int y, m, d;
    sscanf(expiry, "%d-%d-%d", &y, &m, &d);
    return (y == today.tm_year + 1900) && (m == today.tm_mon + 1);
}

int getValidInt(const char *prompt, int min, int max) {
    int value;
    while (1) {
        printf("%s", prompt);
        if (scanf("%d", &value) == 1 && value >= min && value <= max) {
            while (getchar() != '\n'); // flush
            return value;
        }
        typewrite(RED "Invalid input. Enter a number between ", TYPE_DELAY_MS);
        printf("%d and %d.\n", min, max);
        while (getchar() != '\n');
    }
}

// Portable case-insensitive substring search
int containsIgnoreCase(const char *haystack, const char *needle) {
    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    if (nlen == 0) return 1;
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

// --- Hash Table Operations ---
int hash(int id) {
    return id % HASH_SIZE;
}

void insertHash(HashTable *ht, Item *item) {
    int idx = hash(item->id);
    item->hash_next = ht->table[idx];
    ht->table[idx] = item;
}

Item* searchHash(HashTable *ht, int id) {
    int idx = hash(id);
    Item *temp = ht->table[idx];
    while (temp) {
        if (temp->id == id) return temp;
        temp = temp->hash_next;
    }
    return NULL;
}

void removeFromHash(HashTable *ht, int id) {
    int idx = hash(id);
    Item *curr = ht->table[idx];
    Item *prev = NULL;
    while (curr) {
        if (curr->id == id) {
            if (prev) prev->hash_next = curr->hash_next;
            else ht->table[idx] = curr->hash_next;
            return;
        }
        prev = curr;
        curr = curr->hash_next;
    }
}

// --- Min-Heap Operations ---
MinHeap* createHeap(int capacity) {
    MinHeap *h = (MinHeap*)malloc(sizeof(MinHeap));
    if (!h) return NULL;
    h->arr = (Item**)malloc(sizeof(Item*) * capacity);
    h->size = 0;
    h->capacity = capacity;
    return h;
}

void swap(Item **a, Item **b) {
    Item *t = *a;
    *a = *b;
    *b = t;
}

void heapifyUp(MinHeap *h, int idx) {
    if (!h) return;
    int parent = (idx - 1) / 2;
    if (idx && compareDate(h->arr[idx]->expiry, h->arr[parent]->expiry) < 0) {
        swap(&h->arr[idx], &h->arr[parent]);
        heapifyUp(h, parent);
    }
}

void heapifyDown(MinHeap *h, int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    if (left < h->size && compareDate(h->arr[left]->expiry, h->arr[smallest]->expiry) < 0)
        smallest = left;
    if (right < h->size && compareDate(h->arr[right]->expiry, h->arr[smallest]->expiry) < 0)
        smallest = right;

    if (smallest != idx) {
        swap(&h->arr[idx], &h->arr[smallest]);
        heapifyDown(h, smallest);
    }
}

void insertHeap(MinHeap *h, Item *item) {
    if (!h || h->size == h->capacity || !item) return;
    h->arr[h->size] = item;
    heapifyUp(h, h->size);
    h->size++;
}

void rebuildHeap(MinHeap *h, Item *inventory) {
    if (!h) return;
    h->size = 0;
    Item *temp = inventory;
    while (temp) {
        insertHeap(h, temp);
        temp = temp->next;
    }
}

void showTopExpiringItems(MinHeap *h, int n) {
    printf("\n" YELLOW "=== Top %d Items Expiring Soonest ===" RESET "\n", n);
    printf("%-8s %-15s %-12s %-6s %-12s %-10s\n", "ID", "NAME", "CATEGORY", "QTY", "EXPIRY", "DAYS LEFT");
    printf("------------------------------------------------------------------------------\n");
    for (int i = 0; i < n && i < h->size; i++) {
        int days = daysUntilExpiry(h->arr[i]->expiry);
        printf("%-8d %-15s %-12s %-6d %-12s %-10d\n",
               h->arr[i]->id, h->arr[i]->name, h->arr[i]->category,
               h->arr[i]->quantity, h->arr[i]->expiry, days);
    }
}

// --- BST Operations ---
BSTNode* insertBST(BSTNode *root, Item *item) {
    if (!root) {
        BSTNode *node = (BSTNode*)malloc(sizeof(BSTNode));
        node->item = item;
        node->left = node->right = NULL;
        return node;
    }
    if (compareDate(item->expiry, root->item->expiry) < 0)
        root->left = insertBST(root->left, item);
    else
        root->right = insertBST(root->right, item);
    return root;
}

void freeBST(BSTNode *root) {
    if (!root) return;
    freeBST(root->left);
    freeBST(root->right);
    free(root);
}

BSTNode* rebuildBST(Item *inventory) {
    BSTNode *root = NULL;
    Item *temp = inventory;
    while (temp) {
        root = insertBST(root, temp);
        temp = temp->next;
    }
    return root;
}

// --- Linked List Operations ---
void freeInventory(Item *head) {
    while (head) {
        Item *temp = head;
        head = head->next;
        free(temp);
    }
}

Item* deleteItem(Item *head, int id, HashTable *ht) {
    Item *temp = head, *prev = NULL;
    while (temp) {
        if (temp->id == id) {
            if (prev) prev->next = temp->next;
            else head = temp->next;

            removeFromHash(ht, id);
            logAction("DELETE", id, temp->name);
            typewrite(GREEN "Item deleted successfully: " RESET, TYPE_DELAY_MS);
            typewrite(temp->name, TYPE_DELAY_MS);
            typewrite("\n", TYPE_DELAY_MS);
            free(temp);
            return head;
        }
        prev = temp;
        temp = temp->next;
    }
    typewrite(YELLOW "Item not found." RESET "\n", TYPE_DELAY_MS);
    return head;
}

// --- Display Functions ---
void printItemRow(Item *it) {
    printf("%-8d %-15s %-12s %-6d %-12s %-8.2f %-12s\n",
           it->id, it->name, it->category, it->quantity, it->expiry, it->price, it->supplier);
}

void printInventoryList(Item *head) {
    Item *temp = head;
    while(temp) {
        printItemRow(temp);
        temp = temp->next;
    }
}

void printInventoryHeader() {
    printf("--------------------------------------------------------------------------------------------\n");
    printf("%-8s %-15s %-12s %-6s %-12s %-8s %-12s\n", "ID", "NAME", "CATEGORY", "QTY", "EXPIRY", "PRICE", "SUPPLIER");
    printf("--------------------------------------------------------------------------------------------\n");
}

// --- Persistence Functions ---
void saveItems(Item *head) {
    ensureDataDirectory();
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%sinventory.dat", DATA_DIR);
    FILE *fp = fopen(filepath, "wb");
    if(!fp) {
        perror("Failed to save inventory");
        return;
    }
    Item *temp = head;
    while (temp) {
        fwrite(temp, sizeof(Item), 1, fp);
        temp = temp->next;
    }
    fclose(fp);
}

Item* loadItems() {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%sinventory.dat", DATA_DIR);
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    Item *head = NULL;
    while (1) {
        Item *temp = (Item*)malloc(sizeof(Item));
        if (!temp) {
            printf(RED "Memory allocation failed during load." RESET "\n");
            break;
        }
        if (!fread(temp, sizeof(Item), 1, fp)) {
            free(temp);
            break;
        }
        temp->next = head;
        temp->hash_next = NULL;
        head = temp;
    }
    fclose(fp);
    return head;
}

void exportToJSON(Item *head, const char *filename) {
    ensureDataDirectory();
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", DATA_DIR, filename);
    FILE *fp = fopen(filepath, "w");
    if(!fp) {
        perror("Unable to open file for JSON export");
        return;
    }
    fprintf(fp, "[\n");
    Item *temp = head;
    while (temp) {
        fprintf(fp, "  {\n");
        fprintf(fp, "    \"id\": %d,\n", temp->id);
        fprintf(fp, "    \"name\": \"%s\",\n", temp->name);
        fprintf(fp, "    \"quantity\": %d,\n", temp->quantity);
        fprintf(fp, "    \"expiry\": \"%s\",\n", temp->expiry);
        fprintf(fp, "    \"price\": %.2f,\n", temp->price);
        fprintf(fp, "    \"supplier\": \"%s\",\n", temp->supplier);
        fprintf(fp, "    \"category\": \"%s\"\n", temp->category);
        fprintf(fp, "  }%s\n", temp->next ? "," : "");
        temp = temp->next;
    }
    fprintf(fp, "]\n");
    fclose(fp);
    printf(GREEN "Inventory exported to %s" RESET "\n", filepath);
}

// --- Donation Persistence ---
void saveDonationRecord(DonationRecord *rec) {
    ensureDataDirectory();
    FILE *fp = fopen(DONATION_FILE, "ab");
    if (!fp) {
        typewrite(RED "Error saving donation!" RESET "\n", TYPE_DELAY_MS);
        return;
    }
    fwrite(rec, sizeof(DonationRecord), 1, fp);
    fclose(fp);
}

DonationRecord* loadDonations() {
    FILE *fp = fopen(DONATION_FILE, "rb");
    if (!fp) return NULL;

    DonationRecord *head = NULL;
    while (1) {
        DonationRecord *rec = (DonationRecord*)malloc(sizeof(DonationRecord));
        if (!rec) break;
        if (!fread(rec, sizeof(DonationRecord), 1, fp)) {
            free(rec);
            break;
        }
        rec->next = head;
        head = rec;
    }

    fclose(fp);
    return head;
}

void viewDonationHistory() {
    DonationRecord *list = loadDonations();
    if (!list) {
        typewrite(YELLOW "No donation records found." RESET "\n", TYPE_DELAY_MS);
        return;
    }

    typewrite("\n" CYAN "==== Donation History ====" RESET "\n", TYPE_DELAY_MS);

    DonationRecord *temp = list;
    while (temp) {
        printf(GREEN "\nItem: %s (ID: %d)\n" RESET, temp->itemName, temp->itemId);
        printf("Donor: %s\n", temp->donorName);
        printf("Contact: %s\n", temp->donorContact);
        printf("Quantity: %d\n", temp->quantityDonated);
        printf("Date: %s\n", temp->date);
        temp = temp->next;
    }

    // free memory
    while (list) {
        DonationRecord *del = list;
        list = list->next;
        free(del);
    }
}

// --- Business Logic Functions ---
void checkRestock(Item *head, int threshold) {
    Item *temp = head;
    int alert = 0;
    printf("\n" BLUE "=== Restock Alerts (Threshold: %d) ===" RESET "\n", threshold);
    while(temp) {
        if(temp->quantity < threshold) {
            printf(RED "Restock alert:" RESET " %s (ID: %d) quantity is %d. Contact supplier: %s\n",
                   temp->name, temp->id, temp->quantity, temp->supplier);
            alert = 1;
        }
        temp = temp->next;
    }
    if (!alert) printf(GREEN "No restock alerts." RESET "\n");
}

Item* moveToExpired(Item **inventory, Item **expiredList) {
    Item *cur = *inventory, *prev = NULL;
    char nowdate[20];
    getCurrentDate(nowdate, sizeof(nowdate));
    int movedCount = 0;

    while(cur) {
        if(compareDate(cur->expiry, nowdate) < 0) {
            Item *toMove = cur;
            if(prev) prev->next = cur->next;
            else *inventory = cur->next;
            cur = cur->next;
            toMove->next = *expiredList;
            *expiredList = toMove;
            typewrite(RED "Product moved to expired list: " RESET, TYPE_DELAY_MS);
            typewrite(toMove->name, TYPE_DELAY_MS);
            typewrite("\n", TYPE_DELAY_MS);
            logAction("EXPIRED", toMove->id, toMove->name);
            movedCount++;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
    printf(BLUE "\nExpired Item Tally: %d items moved." RESET "\n", movedCount);
    return *expiredList;
}

void updateDynamicDiscounts(Item *head) {
    typewrite("\n" YELLOW "=== Dynamic Discount Recommendations ===" RESET "\n", TYPE_DELAY_MS);
    Item *temp = head;
    int any = 0;
    while (temp) {
        int days = daysUntilExpiry(temp->expiry);
        float discount = 0;

        if (days < 0) discount = 100;
        else if (days <= 2) discount = 50;
        else if (days <= 7) discount = 30;
        else if (days <= 14) discount = 10;
        else discount = 0;

        if (temp->quantity > 100 && discount > 0) discount += 10;

        if (discount > 0) {
            char buf[200];
            snprintf(buf, sizeof(buf), "Discount: %s (ID:%d) → %.0f%% [Days: %d, Qty: %d]\n",
                     temp->name, temp->id, discount, days, temp->quantity);
            typewrite(buf, TYPE_DELAY_MS);
            any = 1;
        }
        temp = temp->next;
    }
    if (!any) typewrite(GREEN "No discount recommendations." RESET "\n", TYPE_DELAY_MS);
}

int isPreferenceMatch(char *itemName, char *preferences) {
    char temp[MAX_PREF];
    strncpy(temp, preferences, MAX_PREF-1);
    temp[MAX_PREF-1] = '\0';
    char *token = strtok(temp, ",");
    while(token) {
        while(*token == ' ') token++;
        if(containsIgnoreCase(itemName, token)) return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

Organization* suggestDonation(Item *item, Organization *orgList) {
    Organization *best = NULL;
    while(orgList) {
        if(isPreferenceMatch(item->name, orgList->preferences)) {
            best = orgList;
            break;
        }
        orgList = orgList->next;
    }
    return best;
}

void mostExpiredCategoryThisMonth(Item *expiredList) {
    char categories[CATEGORY_MAX][MAX_NAME] = {0};
    int counts[CATEGORY_MAX] = {0};
    int catCount = 0;
    Item *temp = expiredList;

    typewrite("\n" BLUE "=== Expired Category Analysis (This Month) ===" RESET "\n", TYPE_DELAY_MS);
    while(temp) {
        if(sameMonth(temp->expiry)) {
            int found = 0;
            for(int i=0; i<catCount; i++) {
                if(strcmp(categories[i], temp->category)==0) {
                    counts[i]++;
                    found=1;
                    break;
                }
            }
            if(!found && catCount < CATEGORY_MAX) {
                strncpy(categories[catCount], temp->category, MAX_NAME-1);
                categories[catCount][MAX_NAME-1]='\0';
                counts[catCount]=1;
                catCount++;
            }
        }
        temp = temp->next;
    }

    int maxInd = -1, maxVal = 0;
    for(int i=0; i<catCount; i++) {
        if(counts[i] > maxVal) {
            maxVal = counts[i];
            maxInd = i;
        }
    }

    if(maxInd != -1)
        printf(GREEN "Most expired category: '%s' [%d items]" RESET "\n", categories[maxInd], counts[maxInd]);
    else
        printf(YELLOW "No expired items this month." RESET "\n");
}

// --- Add Item by Scan / Product DB ---
Item* addItemByScan(Item *head, HashTable *ht, BSTNode **bstRoot, MinHeap *heap) {
    int scanID;
    typewrite(BLUE "Enter Product ID (barcode): " RESET, TYPE_DELAY_MS);
    if (scanf("%d", &scanID) != 1) {
        typewrite(RED "Invalid ID input." RESET "\n", TYPE_DELAY_MS);
        while (getchar() != '\n');
        return head;
    }
    while (getchar() != '\n');

    if (searchHash(ht, scanID)) {
        char buf[100];
        snprintf(buf, sizeof(buf), YELLOW "Item with ID %d already exists!" RESET "\n", scanID);
        typewrite(buf, TYPE_DELAY_MS);
        return head;
    }

    ProductDB *p = NULL;
    for(int i=0; i<productCount; i++) {
        if(products[i].id == scanID) {
            p = &products[i];
            break;
        }
    }

    Item *newItem = (Item*)malloc(sizeof(Item));
    if (!newItem) {
        typewrite(RED "Memory allocation failed!" RESET "\n", TYPE_DELAY_MS);
        return head;
    }
    newItem->hash_next = NULL;
    newItem->next = NULL;

    if(p) {
        newItem->id = p->id;
        strncpy(newItem->name, p->name, MAX_NAME-1);
        newItem->name[MAX_NAME-1] = '\0';
        strncpy(newItem->expiry, p->expiry, 10);
        newItem->expiry[10] = '\0';
        newItem->price = p->price;
        strncpy(newItem->supplier, p->supplier, MAX_NAME-1);
        newItem->supplier[MAX_NAME-1] = '\0';
        strncpy(newItem->category, p->category, MAX_NAME-1);
        newItem->category[MAX_NAME-1] = '\0';
    } else {
        newItem->id = scanID;
        typewrite("Enter Product Name: ", TYPE_DELAY_MS);
        if (scanf(" %49[^\n]", newItem->name) != 1) {
            typewrite(RED "Invalid name input." RESET "\n", TYPE_DELAY_MS);
            free(newItem);
            while (getchar() != '\n');
            return head;
        }
        while (getchar() != '\n');

        do {
            typewrite("Enter Expiry (YYYY-MM-DD): ", TYPE_DELAY_MS);
            if (scanf("%10s", newItem->expiry) != 1) {
                typewrite(RED "Invalid input." RESET "\n", TYPE_DELAY_MS);
                while (getchar() != '\n');
                free(newItem);
                return head;
            }
            while (getchar() != '\n');
            if (isValidDate(newItem->expiry)) break;
            typewrite(RED "Invalid date format. Use YYYY-MM-DD." RESET "\n", TYPE_DELAY_MS);
        } while (1);

        typewrite("Enter Price: ", TYPE_DELAY_MS);
        if (scanf("%f", &newItem->price) != 1) {
            typewrite(RED "Invalid price." RESET "\n", TYPE_DELAY_MS);
            free(newItem);
            while (getchar() != '\n');
            return head;
        }
        while (getchar() != '\n');

        typewrite("Enter Supplier: ", TYPE_DELAY_MS);
        if (scanf(" %49[^\n]", newItem->supplier) != 1) newItem->supplier[0] = '\0';
        while (getchar() != '\n');

        typewrite("Enter Category: ", TYPE_DELAY_MS);
        if (scanf(" %49[^\n]", newItem->category) != 1) newItem->category[0] = '\0';
        while (getchar() != '\n');
    }

    typewrite("Enter Quantity: ", TYPE_DELAY_MS);
    if (scanf("%d", &newItem->quantity) != 1) {
        typewrite(RED "Invalid quantity." RESET "\n", TYPE_DELAY_MS);
        free(newItem);
        while (getchar() != '\n');
        return head;
    }
    while (getchar() != '\n');

    newItem->next = head;
    head = newItem;
    insertHash(ht, newItem);
    *bstRoot = insertBST(*bstRoot, newItem);
    insertHeap(heap, newItem);

    logAction("ADD", newItem->id, newItem->name);
    typewrite(GREEN "Item added: " RESET, TYPE_DELAY_MS);
    typewrite(newItem->name, TYPE_DELAY_MS);
    typewrite(" [Expiry: ", TYPE_DELAY_MS);
    typewrite(newItem->expiry, TYPE_DELAY_MS);
    typewrite("]\n", TYPE_DELAY_MS);
    return head;
}

// Donation process for an item (interactive)
void donateItemInteractive(Item *item) {
    if (!item) {
        typewrite(RED "No item provided for donation." RESET "\n", TYPE_DELAY_MS);
        return;
    }

    typewrite(YELLOW "\n=== Donation Process ===\n" RESET, TYPE_DELAY_MS);

    char donorName[MAX_NAME] = "";
    char donorContact[MAX_NAME] = "";
    int qty = 0;

    typewrite("Enter donor name: ", TYPE_DELAY_MS);
    if (scanf(" %49[^\n]", donorName) != 1) donorName[0] = '\0';
    while (getchar() != '\n');

    typewrite("Enter donor contact: ", TYPE_DELAY_MS);
    if (scanf(" %49[^\n]", donorContact) != 1) donorContact[0] = '\0';
    while (getchar() != '\n');

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "Enter quantity to donate (1 - %d): ", item->quantity);
    qty = getValidInt(prompt, 1, item->quantity);

    // update inventory
    item->quantity -= qty;

    // create record
    DonationRecord rec;
    rec.itemId = item->id;
    strncpy(rec.itemName, item->name, MAX_NAME-1);
    rec.itemName[MAX_NAME-1] = '\0';
    strncpy(rec.donorName, donorName, MAX_NAME-1);
    rec.donorName[MAX_NAME-1] = '\0';
    strncpy(rec.donorContact, donorContact, MAX_NAME-1);
    rec.donorContact[MAX_NAME-1] = '\0';
    rec.quantityDonated = qty;
    getCurrentDate(rec.date, sizeof(rec.date));
    rec.next = NULL;

    // save record to donations.dat
    saveDonationRecord(&rec);

    // log and user feedback
    logAction("DONATE", rec.itemId, rec.itemName);

    typewrite(GREEN "Donation recorded successfully!\n" RESET, TYPE_DELAY_MS);
}

void viewActivityLog() {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%sactivity.log", DATA_DIR);
    FILE *log = fopen(filepath, "r");
    if (!log) {
        typewrite(YELLOW "No activity log found." RESET "\n", TYPE_DELAY_MS);
        return;
    }

    typewrite("\n" CYAN "=== Recent Activity Log ===" RESET "\n", TYPE_DELAY_MS);
    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), log) && count < 20) {
        printf("%s", line);
        count++;
    }
    fclose(log);
}

void generateFullReport(Item *inventory, Item *expiredList, MinHeap *heap) {
    typewrite("\n" MAGENTA "=====================================" RESET "\n", TYPE_DELAY_MS);
    typewrite(MAGENTA "      FULL INVENTORY REPORT" RESET "\n", TYPE_DELAY_MS);
    typewrite(MAGENTA "=====================================" RESET "\n", TYPE_DELAY_MS);

    int total = 0, expiring7 = 0, expiring30 = 0, expired = 0;
    float totalValue = 0;

    Item *temp = inventory;
    while (temp) {
        total++;
        totalValue += temp->price * temp->quantity;
        int days = daysUntilExpiry(temp->expiry);
        if (days < 0) expired++;
        else if (days <= 7) expiring7++;
        else if (days <= 30) expiring30++;
        temp = temp->next;
    }

    typewrite("\n" CYAN "Inventory Summary:" RESET "\n", TYPE_DELAY_MS);
    char buf[256];
    snprintf(buf, sizeof(buf), "  Total Items: %d\n", total);
    typewrite(buf, TYPE_DELAY_MS);
    snprintf(buf, sizeof(buf), "  Total Value: ₹%.2f\n", totalValue);
    typewrite(buf, TYPE_DELAY_MS);
    snprintf(buf, sizeof(buf), "  Expired: %d\n", expired);
    typewrite(buf, TYPE_DELAY_MS);
    snprintf(buf, sizeof(buf), "  Expiring in 7 days: %d\n", expiring7);
    typewrite(buf, TYPE_DELAY_MS);
    snprintf(buf, sizeof(buf), "  Expiring in 30 days: %d\n", expiring30);
    typewrite(buf, TYPE_DELAY_MS);

    if (heap && heap->size > 0) {
        typewrite("\n" YELLOW "Top 5 soonest-to-expire items (preview):" RESET "\n", TYPE_DELAY_MS);
        showTopExpiringItems(heap, 5);
    }

    mostExpiredCategoryThisMonth(expiredList);

    typewrite(MAGENTA "=====================================" RESET "\n", TYPE_DELAY_MS);
}

// --- Additional Helper Functions ---
Item* findAndDelete(Item **headRef, HashTable *ht) {
    int id = getValidInt("Enter Item ID to delete: ", 1, 1000000);
    *headRef = deleteItem(*headRef, id, ht);
    return *headRef;
}

void viewInventory(Item *head) {
    if (!head) {
        typewrite(YELLOW "Inventory is empty." RESET "\n", TYPE_DELAY_MS);
        return;
    }
    printInventoryHeader();
    printInventoryList(head);
}

void viewExpiredList(Item *expiredList) {
    if (!expiredList) {
        typewrite(YELLOW "No expired items." RESET "\n", TYPE_DELAY_MS);
        return;
    }
    typewrite("\n" RED "=== Expired Items ===" RESET "\n", TYPE_DELAY_MS);
    printInventoryHeader();
    printInventoryList(expiredList);
}

// Preload productDB items into inventory (optional)
Item* preloadProducts(Item *head, HashTable *ht, BSTNode **bstRoot, MinHeap *heap) {
    for (int i = 0; i < productCount; i++) {
        Item *newItem = (Item*)malloc(sizeof(Item));
        if (!newItem) continue;
        newItem->id = products[i].id;
        strncpy(newItem->name, products[i].name, MAX_NAME-1);
        newItem->name[MAX_NAME-1] = '\0';
        strncpy(newItem->expiry, products[i].expiry, 10);
        newItem->expiry[10] = '\0';
        newItem->price = products[i].price;
        strncpy(newItem->supplier, products[i].supplier, MAX_NAME-1);
        newItem->supplier[MAX_NAME-1] = '\0';
        strncpy(newItem->category, products[i].category, MAX_NAME-1);
        newItem->category[MAX_NAME-1] = '\0';
        newItem->quantity = 10; // default quantity for preload
        newItem->next = head;
        newItem->hash_next = NULL;
        head = newItem;
        insertHash(ht, newItem);
        *bstRoot = insertBST(*bstRoot, newItem);
        insertHeap(heap, newItem);
    }
    return head;
}

// --- Main ---
int main() {
    enableColors();   // enable ANSI colour support
    typewrite("\nStarting Smart Expiry Tracker ...\n", TYPE_DELAY_MS);

    ensureDataDirectory();

    HashTable ht = {0};
    Item *inventory = NULL;
    Item *expiredList = NULL;
    BSTNode *bstRoot = NULL;
    MinHeap *heap = createHeap(HEAP_CAPACITY);

    // Try load from disk; if none, optionally preload demo products
    inventory = loadItems();
    if (inventory) {
        // rebuild hash, bst, heap from loaded list
        Item *temp = inventory;
        while (temp) {
            temp->hash_next = NULL;
            insertHash(&ht, temp);
            bstRoot = insertBST(bstRoot, temp);
            insertHeap(heap, temp);
            temp = temp->next;
        }
        typewrite(GREEN "Loaded inventory from disk." RESET "\n", TYPE_DELAY_MS);
    } else {
        // If no saved inventory, preload sample products for demo
        inventory = preloadProducts(inventory, &ht, &bstRoot, heap);
        typewrite(YELLOW "No saved inventory found. Preloaded sample products." RESET "\n", TYPE_DELAY_MS);
    }

    int choice;
    while (1) {
        typewrite("\n" CYAN "=== Smart Expiry Tracker Menu ===" RESET "\n", TYPE_DELAY_MS);
        printf("1. View Inventory\n");
        printf("2. Add Item by Scan/Product DB\n");
        printf("3. Delete Item\n");
        printf("4. Move expired items to Expired List\n");
        printf("5. View Expired List\n");
        printf("6. Restock Check\n");
        printf("7. Dynamic Discount Recommendations\n");
        printf("8. Top Expiring Items (Heap)\n");
        printf("9. Export Inventory to JSON\n");
        printf("10. View Activity Log\n");
        printf("11. Generate Full Report\n");
        printf("12. Save & Exit\n");
        printf("13. Donate an Item\n");
        printf("14. View Donation History\n");
        printf("0. Exit without Saving\n");

        choice = getValidInt("Enter choice: ", 0, 14);

        switch (choice) {
            case 1:
                viewInventory(inventory);
                break;
            case 2:
                inventory = addItemByScan(inventory, &ht, &bstRoot, heap);
                break;
            case 3:
                findAndDelete(&inventory, &ht);
                // rebuild heap and bst for consistency
                freeBST(bstRoot);
                bstRoot = rebuildBST(inventory);
                rebuildHeap(heap, inventory);
                break;
            case 4:
                expiredList = moveToExpired(&inventory, &expiredList);
                // rebuild heap and bst after removals
                freeBST(bstRoot);
                bstRoot = rebuildBST(inventory);
                rebuildHeap(heap, inventory);
                break;
            case 5:
                viewExpiredList(expiredList);
                break;
            case 6: {
                int threshold = getValidInt("Enter restock threshold: ", 0, 1000000);
                checkRestock(inventory, threshold);
                break;
            }
            case 7:
                updateDynamicDiscounts(inventory);
                break;
            case 8:
                rebuildHeap(heap, inventory); // ensure heap reflects current inventory
                showTopExpiringItems(heap, 10);
                break;
            case 9: {
                char fname[128];
                typewrite("Enter filename (e.g., export.json): ", TYPE_DELAY_MS);
                if (scanf(" %127[^\n]", fname) == 1) {
                    exportToJSON(inventory, fname);
                } else {
                    typewrite(RED "Invalid filename." RESET "\n", TYPE_DELAY_MS);
                }
                while (getchar() != '\n');
                break;
            }
            case 10:
                viewActivityLog();
                break;
            case 11:
                rebuildHeap(heap, inventory);
                typewrite("Generating full report...\n", TYPE_DELAY_MS);
                generateFullReport(inventory, expiredList, heap);
                break;
            case 12:
                saveItems(inventory);
                typewrite(GREEN "Inventory saved. Exiting..." RESET "\n", TYPE_DELAY_MS);
                // free memory
                freeInventory(inventory);
                freeInventory(expiredList);
                freeBST(bstRoot);
                if (heap) {
                    free(heap->arr);
                    free(heap);
                }
                return 0;
            case 13: {
                int id = getValidInt("Enter item ID to donate: ", 1, 1000000);
                Item *it = searchHash(&ht, id);
                if (!it) {
                    typewrite(RED "Item not found!" RESET "\n", TYPE_DELAY_MS);
                } else {
                    donateItemInteractive(it);
                }
                break;
            }
            case 14:
                viewDonationHistory();
                break;
            case 0:
                typewrite(YELLOW "Exiting without saving. Goodbye!" RESET "\n", TYPE_DELAY_MS);
                // free memory
                freeInventory(inventory);
                freeInventory(expiredList);
                freeBST(bstRoot);
                if (heap) {
                    free(heap->arr);
                    free(heap);
                }
                return 0;
            default:
                typewrite(RED "Invalid choice." RESET "\n", TYPE_DELAY_MS);
        }
    }

    return 0;
}

