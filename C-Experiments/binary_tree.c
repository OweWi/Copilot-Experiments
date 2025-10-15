#include <stdio.h>
/**
 * @brief Node structure representing a single node in a binary tree.
 *
 * This struct contains an integer data field and two pointers to child nodes (left and right).
 */
typedef struct Node {
    int data;           /**< Data stored at this node. */
    struct Node *left;  /**< Pointer to the left child of this node. */
    struct Node *right; /**< Pointer to the right child of this node. */
} Node;

#define MAX_NUM_NODES 7
Node nodes[MAX_NUM_NODES];
int count = 0;
/**
 * @brief Allocates a new node in the binary tree with the given data.
 *
 * If the maximum number of nodes has been reached, it prints an error message and returns NULL.
 *
 * @param[in] data The integer value to be stored at the new node.
 *
 * @return A pointer to the newly allocated Node struct if successful, or NULL on failure.
 */
Node *NewNode(int data)
{
    if (count < MAX_NUM_NODES)
    {
        Node *temp = &nodes[count++]; // Allocate memory for new node

        temp->data = data;  // Assign given data to the node's data field
        temp->left = NULL;  // Set left and right children to NULL
        temp->right = NULL; // Set left and right children to NULL
        return temp;
    }
    else
    {
        printf("Error: Maximum number of nodes reached.\n");
        return NULL;
    }
}

void inOrder(Node *node)
{
    if (node == NULL) // If current node is NULL, stop recursion
        return;

    inOrder(node->left);       ///< Go left and continue traversal
    printf("%d ", node->data); // Print data of root node
    inOrder(node->right);      ///< Go right and continue traversal
}

Node *search(Node *root, int key)
{
    if (root == NULL || root->data == key) // If root or current node data matches with key, return pointer
        return root;

    if (root->data < key) // If key is greater than current node's data, go right subtree
        return search(root->right, key);
    else // Else go left subtree
        return search(root->left, key);
}

int main()
{
    Node *root = NULL; // Initialize root as NULL for an empty tree
    root = NewNode(50);
    root->left = NewNode(30);
    root->right = NewNode(70);
    root->left->left = NewNode(20);
    root->left->right = NewNode(40);
    root->right->left = NewNode(60);
    root->right->right = NewNode(80);

    printf("In Order: ");
    inOrder(root);

    int key = 60;
    Node *result = search(root, key);
    if (result != NULL)
        printf("\nElement %d found\n", result->data); // If node is not null, it means the element was found in tree. Print its data.
    else
        printf("\nElement not found in the tree\n"); // Node is null meaning that key wasn't found in tree

    return 0;
}
