# ExamGrid — DSA Viva Notes

## Problem statement

ExamGrid simulates the administrative workflow of a large entrance examination: candidate records, limited centre capacity, seat assignment, candidate check-in, audit history, and recovery when an examination centre becomes unavailable.

It uses synthetic records only. It is not an official NTA system.

## Why each data structure is used

### 1. Arrays and 2D arrays

Candidate records and centre records are kept in fixed-capacity arrays. Every hall has a 2D `seats[row][column]` array. Each cell stores a candidate array index, or `-1` when the seat is empty.

This directly models physical seating and makes capacity explicit.

### 2. Binary Search Tree (BST)

Each BST node stores a candidate-array index and left/right child indices. The candidate's application ID is the key. The implementation supports lookup by ID and inorder traversal for sorted candidate listings.

- Average search/insert: `O(log n)` when the tree is reasonably balanced.
- Worst-case search/insert: `O(n)` for a skewed tree.
- Inorder traversal: `O(n)`.

The tree is intentionally a basic BST, not AVL or red-black, to stay within introductory DSA.

### 3. Circular queue

The check-in queue stores candidate-array indices. `enqueue` appends at the tail; `dequeue` removes from the head.

- Enqueue: `O(1)`.
- Dequeue: `O(1)`.

This demonstrates first-in, first-out processing.

### 4. Linked list

Every significant operation is appended to a singly linked audit list with a timestamp. The list preserves event order and can be traversed for display or persistence.

- Append: `O(1)` because a tail pointer is maintained.
- Display/export: `O(m)` for `m` events.

### 5. Stack

The allocation undo stack stores snapshots of candidate assignments and centre availability. The check-in undo history is another stack of candidate indices.

- Push/pop: `O(1)` for the stack operation itself.
- Restoring an allocation snapshot: `O(n + occupied seats)` because seat grids must be reconstructed.

## Seat allocation algorithm

For each candidate:

1. Search active centres matching preference 1.
2. If no seat is available there, try preference 2.
3. If still unsuccessful, try other active centres.
4. Within a centre, scan halls and seats in row-major order and claim the first empty seat.
5. If no seat exists, leave the candidate unallocated and include them in the exception count.

Capacity is never intentionally exceeded because a seat is assigned only if the corresponding cell equals `-1`.

## Centre failure simulation

1. Save an undo snapshot.
2. Mark the chosen centre unavailable.
3. Identify candidates whose saved assignments belong to that centre.
4. Clear those assignments and rebuild seat grids for remaining centres.
5. Reallocate displaced candidates using their stated preferences and available capacity.
6. Report how many were displaced, reallocated, and left unallocated.
7. Allow the operator to undo the simulation before locking the plan.

## Suggested live demo

1. Open the dashboard and explain the 600 synthetic candidates and three centres.
2. Allocate seating and open a hall's 2D seating grid.
3. Search for an application ID and show its assigned seat via the BST.
4. Prepare the check-in queue and process one candidate.
5. Undo the most recent check-in using the stack.
6. Simulate a centre failure and show reallocation.
7. Undo the failure, open the linked-list audit trail, and export reports.

## Limitations to acknowledge honestly

- This is an educational local simulator, not a real national exam platform.
- The basic BST is not self-balancing.
- The allocation strategy is a simple preference-first, first-free-seat heuristic, not a globally optimal allocation algorithm.
- Candidate data is synthetic. Do not use real sensitive candidate records for a class demonstration.
