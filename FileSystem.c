#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <winfsp/winfsp.h>

#define MAX_FILES 100
#define MAX_FILENAME_LENGTH 50
#define MAX_PATH_LENGTH 256
#define BLOCK_SIZE 1024
#define MAX_BLOCKS_PER_FILE 10

// Data structures
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    int inode_number;
    char parent_directory[MAX_PATH_LENGTH];
} DirectoryEntry;

typedef struct {
    int inode_number;
    int file_size;
    int index_block;
} Inode;

typedef struct {
    int blocks[MAX_BLOCKS_PER_FILE];
} IndexBlock;

typedef struct {
    char data[BLOCK_SIZE];
} Block;

typedef struct {
    int free_block_count;
    int free_block_list[MAX_FILES * MAX_BLOCKS_PER_FILE];
    int inode_count;
} Superblock;

// Global variables
Superblock sb;
DirectoryEntry root_dir[MAX_FILES];
Inode inodes[MAX_FILES];
IndexBlock index_blocks[MAX_FILES];
Block disk_blocks[MAX_FILES * MAX_BLOCKS_PER_FILE];

GtkWidget *window;
GtkWidget *file_list;
GtkWidget *dir_list;
GtkWidget *entry_filename;
GtkWidget *entry_directory;
GtkWidget *entry_content;
GtkWidget *current_dir_label;
GtkTextBuffer *content_buffer;
GtkTextBuffer *write_buffer;
GtkWidget *status_bar;
char current_directory[MAX_PATH_LENGTH] = "/";
char previous_directory[MAX_PATH_LENGTH] = "/";
char history_directories[MAX_FILES][MAX_PATH_LENGTH];
int history_index = 0;

// Function prototypes
void initialize_fs();
int allocate_block();
int create_directory_entry(const char *filename, int inode_num, const char *parent_directory);
void delete_directory_entry(int index);
int create_file(const char *filename, const char *parent_directory);
int write_file(int inode_num, const char *data, int length);
int read_file(int inode_num, char *buffer, int length);
int find_directory_entry(const char *filename, const char *directory);
void update_file_list();
void update_directory_list();
void update_current_dir_label();
void on_create_directory_clicked(GtkWidget *widget, gpointer data);
void on_change_directory_clicked(GtkWidget *widget, gpointer data);
void on_previous_directory_clicked(GtkWidget *widget, gpointer data);
void on_create_file_clicked(GtkWidget *widget, gpointer data);
void on_write_to_file_clicked(GtkWidget *widget, gpointer data);
void on_delete_clicked(GtkWidget *widget, gpointer data);
void on_view_contents_clicked(GtkWidget *widget, gpointer data);
void on_file_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data);
void on_dir_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data);

// Initialize the file system
void initialize_fs() {
    sb.free_block_count = MAX_FILES * MAX_BLOCKS_PER_FILE;
    for (int i = 0; i < sb.free_block_count; i++) {
        sb.free_block_list[i] = i;
    }
    sb.inode_count = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        root_dir[i].inode_number = -1;
    }
}

// Allocate a free block
int allocate_block() {
    if (sb.free_block_count == 0) {
        return -1;
    }
    return sb.free_block_list[--sb.free_block_count];
}

// Create a new directory entry
int create_directory_entry(const char *filename, int inode_num, const char *parent_directory) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number == -1) {
            strcpy(root_dir[i].filename, filename);
            root_dir[i].inode_number = inode_num;
            strcpy(root_dir[i].parent_directory, parent_directory);
            return i;
        }
    }
    return -1;
}

// Delete a directory entry
void delete_directory_entry(int index) {
    root_dir[index].inode_number = -1;
}

// Create a new file
int create_file(const char *filename, const char *parent_directory) {
    if (find_directory_entry(filename, parent_directory) != -1) {
        printf("File or directory already exists.\n");
        return -1;
    }

    int inode_num = sb.inode_count++;
    Inode *inode = &inodes[inode_num];
    inode->inode_number = inode_num;
    inode->file_size = 0;
    inode->index_block = allocate_block();

    if (inode->index_block == -1) {
        printf("No free blocks available for index block.\n");
        return -1;
    }

    if (create_directory_entry(filename, inode_num, parent_directory) == -1) {
        printf("No free directory entries available.\n");
        return -1;
    }

    return inode_num;
}

// Write data to a file
int write_file(int inode_num, const char *data, int length) {
    Inode *inode = &inodes[inode_num];
    int remaining_length = length;
    int offset = 0;

    if (inode->index_block == -1) {
        inode->index_block = allocate_block();
        if (inode->index_block == -1) {
            printf("No free blocks available for index block.\n");
            return -1;
        }
    }

    for (int i = 0; i < MAX_BLOCKS_PER_FILE && remaining_length > 0; i++) {
        int block_index = allocate_block();
        if (block_index == -1) {
            printf("No free blocks available for data block.\n");
            return -1;
        }
        index_blocks[inode->index_block].blocks[i] = block_index;
        int chunk_size = remaining_length < BLOCK_SIZE ? remaining_length : BLOCK_SIZE;
        strncpy(disk_blocks[block_index].data, data + offset, chunk_size);
        remaining_length -= chunk_size;
        offset += chunk_size;
        inode->file_size += chunk_size;
    }

    return 0;
}

// Read data from a file
int read_file(int inode_num, char *buffer, int length) {
    Inode *inode = &inodes[inode_num];
    int remaining_length = length;
    int offset = 0;

    if (inode->index_block == -1) {
        printf("Index block not allocated.\n");
        return -1;
    }

    for (int i = 0; i < MAX_BLOCKS_PER_FILE && remaining_length > 0; i++) {
        int block_index = index_blocks[inode->index_block].blocks[i];
        if (block_index == -1) {
            break;
        }
        int chunk_size = remaining_length < BLOCK_SIZE ? remaining_length : BLOCK_SIZE;
        strncpy(buffer + offset, disk_blocks[block_index].data, chunk_size);
        remaining_length -= chunk_size;
        offset += chunk_size;
    }

    return 0;
}

// Find a directory entry
int find_directory_entry(const char *filename, const char *directory) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 &&
            strcmp(root_dir[i].filename, filename) == 0 &&
            strcmp(root_dir[i].parent_directory, directory) == 0) {
            return i;
        }
    }
    return -1;
}

// Function to view file contents
void on_view_contents_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = gtk_entry_get_text(GTK_ENTRY(entry_filename));
    int dir_index = find_directory_entry(filename, current_directory);
    if (dir_index == -1) {
        gtk_text_buffer_set_text(content_buffer, "File not found.", -1);
        return;
    }
    int inode_num = root_dir[dir_index].inode_number;
    char buffer[MAX_BLOCKS_PER_FILE * BLOCK_SIZE];
    read_file(inode_num, buffer, MAX_BLOCKS_PER_FILE * BLOCK_SIZE);
    gtk_text_buffer_set_text(content_buffer, buffer, -1);
}

// Function to write to a file
void on_write_to_file_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = gtk_entry_get_text(GTK_ENTRY(entry_filename));
    int dir_index = find_directory_entry(filename, current_directory);
    if (dir_index == -1) {
        gtk_text_buffer_set_text(content_buffer, "File not found.", -1);
        return;
    }
    int inode_num = root_dir[dir_index].inode_number;
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(write_buffer, &start);
    gtk_text_buffer_get_end_iter(write_buffer, &end);
    gchar *text = gtk_text_buffer_get_text(write_buffer, &start, &end, FALSE);
    write_file(inode_num, text, strlen(text));
    g_free(text);
    gtk_text_buffer_set_text(content_buffer, "File written successfully.", -1);
}

// Function to change to the previous directory
void on_previous_directory_clicked(GtkWidget *widget, gpointer data) {
    if (history_index > 0) {
        strcpy(current_directory, history_directories[--history_index]);
        update_current_dir_label();
        update_file_list();
        update_directory_list();
    } else {
        gtk_text_buffer_set_text(content_buffer, "No previous directory.", -1);
    }
}

// Function to update the current directory label
void update_current_dir_label() {
    char label_text[MAX_PATH_LENGTH + 20];
    snprintf(label_text, sizeof(label_text), "Current Directory: %s", current_directory);
    gtk_label_set_text(GTK_LABEL(current_dir_label), label_text);
}

// Function to update the file list in the UI
void update_file_list() {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(file_list)));
    gtk_list_store_clear(store);

    GtkTreeIter iter;
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, current_directory) == 0) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, root_dir[i].filename, -1);
        }
    }
}

// Function to update the directory list in the UI
void update_directory_list() {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dir_list)));
    gtk_list_store_clear(store);

    GtkTreeIter iter;
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, current_directory) == 0) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, root_dir[i].filename, -1);
        }
    }
}

// Function to handle file list row activation
void on_file_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    gtk_tree_model_get_iter(model, &iter, path);
    gchar *filename;
    gtk_tree_model_get(model, &iter, 0, &filename, -1);
    gtk_entry_set_text(GTK_ENTRY(entry_filename), filename);
    g_free(filename);
}

// Function to handle directory list row activation
void on_dir_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    gtk_tree_model_get_iter(model, &iter, path);
    gchar *directory;
    gtk_tree_model_get(model, &iter, 0, &directory, -1);
    strcat(current_directory, directory);
    strcat(current_directory, "/");
    update_current_dir_label();
    update_file_list();
    update_directory_list();
    g_free(directory);
}

// Function to create a new directory
void on_create_directory_clicked(GtkWidget *widget, gpointer data) {
    const char *dirname = gtk_entry_get_text(GTK_ENTRY(entry_directory));
    if (create_file(dirname, current_directory) != -1) {
        update_file_list();
        update_directory_list();
    } else {
        gtk_text_buffer_set_text(content_buffer, "Failed to create directory.", -1);
    }
}

// Function to change directory
void on_change_directory_clicked(GtkWidget *widget, gpointer data) {
    const char *dirname = gtk_entry_get_text(GTK_ENTRY(entry_directory));
    char new_directory[MAX_PATH_LENGTH];
    snprintf(new_directory, sizeof(new_directory), "%s%s/", current_directory, dirname);
    int dir_index = find_directory_entry(dirname, current_directory);
    if (dir_index != -1) {
        strcpy(previous_directory, current_directory);
        strcpy(current_directory, new_directory);
        update_current_dir_label();
        update_file_list();
        update_directory_list();
    } else {
        gtk_text_buffer_set_text(content_buffer, "Directory not found.", -1);
    }
}

// Function to delete a file or directory
void on_delete_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = gtk_entry_get_text(GTK_ENTRY(entry_filename));
    int dir_index = find_directory_entry(filename, current_directory);
    if (dir_index != -1) {
        delete_directory_entry(dir_index);
        update_file_list();
        update_directory_list();
        gtk_text_buffer_set_text(content_buffer, "File deleted.", -1);
    } else {
        gtk_text_buffer_set_text(content_buffer, "File not found.", -1);
    }
}

// Main function
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    initialize_fs();

    // Create the main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Simple File System");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create a grid layout
    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    // Create the directory entry field and button
    entry_directory = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), entry_directory, 0, 0, 1, 1);
    GtkWidget *button_create_directory = gtk_button_new_with_label("Create Directory");
    g_signal_connect(button_create_directory, "clicked", G_CALLBACK(on_create_directory_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_create_directory, 1, 0, 1, 1);

    GtkWidget *button_change_directory = gtk_button_new_with_label("Change Directory");
    g_signal_connect(button_change_directory, "clicked", G_CALLBACK(on_change_directory_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_change_directory, 2, 0, 1, 1);

    GtkWidget *button_previous_directory = gtk_button_new_with_label("Previous Directory");
    g_signal_connect(button_previous_directory, "clicked", G_CALLBACK(on_previous_directory_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_previous_directory, 3, 0, 1, 1);

    // Create the file entry field and buttons
    entry_filename = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), entry_filename, 0, 1, 1, 1);
    GtkWidget *button_create_file = gtk_button_new_with_label("Create File");
    g_signal_connect(button_create_file, "clicked", G_CALLBACK(on_create_file_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_create_file, 1, 1, 1, 1);

    GtkWidget *button_write_to_file = gtk_button_new_with_label("Write to File");
    g_signal_connect(button_write_to_file, "clicked", G_CALLBACK(on_write_to_file_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_write_to_file, 2, 1, 1, 1);

    GtkWidget *button_delete = gtk_button_new_with_label("Delete");
    g_signal_connect(button_delete, "clicked", G_CALLBACK(on_delete_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button_delete, 3, 1, 1, 1);

    // Create the file list and directory list views
    file_list = gtk_tree_view_new();
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list), column);
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(file_list), GTK_TREE_MODEL(store));
    gtk_grid_attach(GTK_GRID(grid), file_list, 0, 2, 1, 1);

    dir_list = gtk_tree_view_new();
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Directories", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dir_list), column);
    store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(dir_list), GTK_TREE_MODEL(store));
    gtk_grid_attach(GTK_GRID(grid), dir_list, 1, 2, 1, 1);

    // Create the content buffer and view
    content_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *content_view = gtk_text_view_new_with_buffer(content_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(content_view), FALSE);
    gtk_grid_attach(GTK_GRID(grid), content_view, 0, 3, 1, 1);

    // Create the write buffer and view
    write_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *write_view = gtk_text_view_new_with_buffer(write_buffer);
    gtk_grid_attach(GTK_GRID(grid), write_view, 1, 3, 1, 1);

    // Create the current directory label
    current_dir_label = gtk_label_new("Current Directory: /");
    gtk_grid_attach(GTK_GRID(grid), current_dir_label, 0, 4, 1, 1);

    // Create the status bar
    status_bar = gtk_statusbar_new();
    gtk_grid_attach(GTK_GRID(grid), status_bar, 0, 5, 1, 1);

    // Connect signals for file and directory list activation
    g_signal_connect(file_list, "row-activated", G_CALLBACK(on_file_list_row_activated), NULL);
    g_signal_connect(dir_list, "row-activated", G_CALLBACK(on_dir_list_row_activated), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
