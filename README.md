# ExamGrid — C++ DSA + Browser Frontend

ExamGrid is an educational examination-centre and seating-operations simulator. The website is served by a local C++17 process. **The actual candidate records, allocations, check-in queue, search, audit log, and state changes are handled by C++; the browser is a frontend, not a JavaScript mock.**

All default records are synthetic. The project is not affiliated with NTA or any official examination authority and must not be used with real candidate identity data.

## Run the website (macOS / Linux)

1. Install a C++ compiler such as `g++` (Apple Clang also works with the included compiler command).
2. Open Terminal and enter the project folder.
3. Run:

   ```bash
   ./run.sh
   ```

4. Open **http://127.0.0.1:8080** in your browser.
5. Leave the terminal running while presenting the website. Press **Ctrl+C** in that terminal to stop it.

The script compiles `main.cpp` and starts the C++ HTTP server. If port 8080 is already in use, run:

```bash
EXAMGRID_PORT=8081 ./run.sh
```

Then open `http://127.0.0.1:8081`.

> Run `./run.sh` from this project directory (the script changes into its own directory automatically). The server binds to `127.0.0.1` only and is intended for a local presentation, not network/public deployment.

## What is included

- **Overview:** candidate count, allocated seats, free capacity, check-ins, centre availability, and recent events.
- **Candidate directory:** BST-based ID lookup, inorder listing, registration, and CSV import/export.
- **Seating plan:** select a centre and hall, inspect each seat in the C++ 2D array, and inspect the assigned candidate.
- **Operations:** allocate seats, prepare/process FIFO check-in, undo a check-in, simulate centre failure, undo an allocation snapshot, add centres, and lock/unlock the plan.
- **Audit trail:** chronological events held in a manually implemented linked list.
- **Always-visible DSA explanation:** the compact panel above the current screen updates to explain the data structure, operation steps, and time complexity.
- **Persistence:** the backend saves state in `examgrid_state.txt` after successful mutations.

## Data structures implemented manually in C++

- **Fixed arrays:** candidates, centres and audit/undo-related storage.
- **Binary Search Tree:** candidate application-ID index; search and inorder listing.
- **2D arrays:** each hall's seat grid; `-1` represents an empty seat.
- **Circular FIFO queue:** check-in processing.
- **Singly linked list:** timestamped audit trail, appended using a tail pointer.
- **LIFO stack:** recent check-in undo history and allocation snapshots.

The frontend is a deliberately minimal, neutral interface with plain navigation, readable tables and a visual seating grid. The DSA explanation stays visible across screens and changes based on the operation being demonstrated. The frontend lives in `web/index.html`, `web/styles.css`, and `web/app.js`. `web/app.js` calls HTTP endpoints served by the C++ process; all state-changing logic is implemented in `main.cpp`.

## Suggested live demonstration

1. On Overview, explain the candidate array, BST index and capacity metrics.
2. Click **Allocate seating**. Explain preference-first allocation and the 2D seat arrays.
3. Open **Seating plan**, choose a centre and hall, and click occupied seats.
4. Open **Candidates** and inspect a candidate. The DSA panel shows the number of BST comparisons.
5. Open **Operations** → **Prepare queue**, then **Process next** to demonstrate FIFO behaviour. Use **Undo last check-in** to demonstrate the manual stack.
6. Before checking in candidates (or after resetting demo data), run **Simulate centre failure**. Explain snapshot push, displaced candidates, reallocation and remaining capacity.
7. Click **Undo allocation change** to restore the snapshot.
8. Open **Audit trail** to demonstrate append-only linked-list events.
9. Add a centre and explain how its hall arrays are initialised, or register a candidate and show BST insertion.

If you've already processed a check-in, centre-failure simulation is intentionally disabled until all check-ins are reversed/reset. To start fresh, stop the server with Ctrl+C, delete `examgrid_state.txt`, and run `./run.sh` again. The next run seeds 600 synthetic candidates across three centres.

## CSV import format

```csv
candidate_id,name,preference1,preference2
EG900001,Sample Candidate,Indore,Nashik
EG900002,Another Candidate,Pune,Indore
```

The importer expects exactly four fields. Duplicate IDs and malformed rows are skipped and counted.

## Build directly

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o examgrid
./examgrid
```

## Limitations

- The small BST is intentionally unbalanced to demonstrate fundamental BST operations; worst-case search may be O(N).
- The built-in web server is a compact local-only teaching server, not a hardened production HTTP server.
- Real national exam deployments involve security, privacy, fairness, accessibility and operational requirements far beyond this simulation.
