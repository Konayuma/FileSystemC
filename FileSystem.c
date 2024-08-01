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
int is_directory_empty(const char *directory);
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

// Check if a directory is empty
int is_directory_empty(const char *directory) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, directory) == 0) {
            return 0;
        }
    }
    return 1;
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
        history_index--;
        strcpy(current_directory, history_directories[history_index]);
        update_file_list();
        update_directory_list();
        update_current_dir_label();
    }
}

// Function to change directory
void on_change_directory_clicked(GtkWidget *widget, gpointer data) {
    const char *directory = gtk_entry_get_text(GTK_ENTRY(entry_directory));
    char new_directory[MAX_PATH_LENGTH];
    
    // Save current directory to history before changing
    if (history_index < MAX_FILES - 1) {
        strcpy(history_directories[++history_index], current_directory);
    }
    
    if (strcmp(directory, "..") == 0) {
        // Move up one level
        char *last_slash = strrchr(current_directory, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
        }
        if (strlen(current_directory) == 0) {
            strcpy(current_directory, "/");
        }
    } else {
        // Move into specified directory
        if (strcmp(current_directory, "/") != 0) {
            snprintf(new_directory, sizeof(new_directory), "%s/%s", current_directory, directory);
        } else {
            snprintf(new_directory, sizeof(new_directory), "%s%s", current_directory, directory);
        }
        strcpy(current_directory, new_directory);
    }
    update_file_list();
    update_directory_list();
    update_current_dir_label();
}

// Function to delete a directory
void delete_directory(const char *directory) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, directory) == 0) {
            if (is_directory_empty(root_dir[i].filename) == 0) {
                gtk_text_buffer_set_text(content_buffer, "Directory is not empty.", -1);
                return;
            }
            delete_directory_entry(i);
        }
    }
    gtk_text_buffer_set_text(content_buffer, "Directory deleted successfully.", -1);
}

// Function to delete a file or directory
void on_delete_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = gtk_entry_get_text(GTK_ENTRY(entry_filename));
    int dir_index = find_directory_entry(filename, current_directory);
    if (dir_index == -1) {
        gtk_text_buffer_set_text(content_buffer, "File or directory not found.", -1);
        return;
    }

    // Check if it's a directory
    if (is_directory_empty(root_dir[dir_index].filename) == 0) {
        delete_directory(root_dir[dir_index].filename);
    } else {
        delete_directory_entry(dir_index);
        gtk_text_buffer_set_text(content_buffer, "File deleted successfully.", -1);
    }
    update_file_list();
    update_directory_list();
}

// Update file list in UI
void update_file_list() {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(file_list)));
    gtk_list_store_clear(store);

    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, current_directory) == 0) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, root_dir[i].filename, -1);
        }
    }
}

// Update directory list in UI
void update_directory_list() {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dir_list)));
    gtk_list_store_clear(store);

    for (int i = 0; i < MAX_FILES; i++) {
        if (root_dir[i].inode_number != -1 && strcmp(root_dir[i].parent_directory, current_directory) == 0) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, root_dir[i].filename, -1);
        }
    }
}

// Update current directory label
void update_current_dir_label() {
    gtk_label_set_text(GTK_LABEL(current_dir_label), current_directory);
}

// Create directory
void on_create_directory_clicked(GtkWidget *widget, gpointer data) {
    const char *directory_name = gtk_entry_get_text(GTK_ENTRY(entry_directory));
    if (create_directory_entry(directory_name, -1, current_directory) != -1) {
        update_directory_list();
        gtk_text_buffer_set_text(content_buffer, "Directory created successfully.", -1);
    } else {
        gtk_text_buffer_set_text(content_buffer, "Failed to create directory.", -1);
    }
}

// Create a file
void on_create_file_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = gtk_entry_get_text(GTK_ENTRY(entry_filename));
    if (create_file(filename, current_directory) != -1) {
        update_file_list();
        gtk_text_buffer_set_text(content_buffer, "File created successfully.", -1);
    } else {
        gtk_text_buffer_set_text(content_buffer, "Failed to create file.", -1);
    }
}

// Handle file list row activation
void on_file_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar *filename;
        gtk_tree_model_get(model, &iter, 0, &filename, -1);
        gtk_entry_set_text(GTK_ENTRY(entry_filename), filename);
        g_free(filename);
    }
}

// Handle directory list row activation
void on_dir_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar *directory;
        gtk_tree_model_get(model, &iter, 0, &directory, -1);
        gtk_entry_set_text(GTK_ENTRY(entry_directory), directory);
        g_free(directory);
    }
}

// Main function
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    initialize_fs();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Simple File System");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    current_dir_label = gtk_label_new(current_directory);
    gtk_box_pack_start(GTK_BOX(vbox), current_dir_label, FALSE, FALSE, 0);

    file_list = gtk_tree_view_new();
    GtkListStore *file_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(file_list), GTK_TREE_MODEL(file_store));
    g_object_unref(file_store);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list), column);
    gtk_box_pack_start(GTK_BOX(vbox), file_list, TRUE, TRUE, 0);
    g_signal_connect(file_list, "row-activated", G_CALLBACK(on_file_list_row_activated), NULL);

    dir_list = gtk_tree_view_new();
    GtkListStore *dir_store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(dir_list), GTK_TREE_MODEL(dir_store));
    g_object_unref(dir_store);
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Directories", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dir_list), column);
    gtk_box_pack_start(GTK_BOX(vbox), dir_list, TRUE, TRUE, 0);
    g_signal_connect(dir_list, "row-activated", G_CALLBACK(on_dir_list_row_activated), NULL);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new("File:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    entry_filename = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox), entry_filename, TRUE, TRUE, 0);

    GtkWidget *button = gtk_button_new_with_label("Create File");
    g_signal_connect(button, "clicked", G_CALLBACK(on_create_file_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    button = gtk_button_new_with_label("Delete");
    g_signal_connect(button, "clicked", G_CALLBACK(on_delete_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    button = gtk_button_new_with_label("View Contents");
    g_signal_connect(button, "clicked", G_CALLBACK(on_view_contents_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

    label = gtk_label_new("Directory:");
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    entry_directory = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox2), entry_directory, TRUE, TRUE, 0);

    button = gtk_button_new_with_label("Create Directory");
    g_signal_connect(button, "clicked", G_CALLBACK(on_create_directory_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), button, FALSE, FALSE, 0);

    button = gtk_button_new_with_label("Change Directory");
    g_signal_connect(button, "clicked", G_CALLBACK(on_change_directory_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), button, FALSE, FALSE, 0);

    button = gtk_button_new_with_label("Previous Directory");
    g_signal_connect(button, "clicked", G_CALLBACK(on_previous_directory_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), button, FALSE, FALSE, 0);

    GtkWidget *hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox3, FALSE, FALSE, 0);

    GtkWidget *scroll_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(hbox3), scroll_win, TRUE, TRUE, 0);

    content_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *text_view = gtk_text_view_new_with_buffer(content_buffer);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll_win), text_view);

    GtkWidget *write_scroll_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(write_scroll_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(hbox3), write_scroll_win, TRUE, TRUE, 0);

    write_buffer = gtk_text_buffer_new(NULL);
    GtkWidget *write_view = gtk_text_view_new_with_buffer(write_buffer);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(write_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(write_scroll_win), write_view);

    button = gtk_button_new_with_label("Write to File");
    g_signal_connect(button, "clicked", G_CALLBACK(on_write_to_file_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
