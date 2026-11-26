### Sudoku Solver and Generator Library

A C++ Sudoku solver and generator focused on performance, bitwise operations and efficient search algorithms.

## Board

Since performance is a major focus of this library, the board is represented as an array of integers. 
However, most operations rely on bitmasks and bitwise operations rather than directly traversing the board.

The implementation also maintains an \emptyIndex``, an array containing the indexes of empty cells. 
This avoids having to iterate over the entire board every time an empty cell needs to be found.

## Solver
The solver implements a recursive backtracking algorithm using MRV 
(Minimum Remaining Values) with early returns.

It may not be the most sophisticated solving algorithm, but that was not the main goal of this project. 
The focus was to build an algorithm that was simple enough while still delivering excellent performance.

## Generator
The generator uses a randomized top-down approach, also known as digging.
It starts with a completed Sudoku grid and progressively removes numbers while preserving a unique solution.

Currently, the generated puzzles do not always reach the MID or HARD difficulty levels. 
This is a limitation of the current generation strategy: producing puzzles that require more complex solving techniques 
is considerably harder than simply generating random puzzles.
As with the solver, the priority was performance rather than implementing the most sophisticated generation algorithm.

## Public API
The library provides a unified public interface through Sudoku.hpp.

It exposes two main operations:

- solve(): Attempts to solve a given Sudoku board and returns a status code.

- printSolution(): Solves the board and prints the resulting solution.

For generation, the API provides two options:

- generateBoard(): Generates a Sudoku using an internally initialized random generator.

- generateBoard(difficulty, gen): Generates a Sudoku using a user-provided std::mt19937 generator, allowing control over the random seed and reproducibility.

## Performance and Benchmarking

Performance was a major part of this project.

Profiling helped identify expensive operations, particularly std::popcount, which became significantly faster after enabling native processor instructions.
A second optimization improved the MRV cell selection by maintaining a bitmask of empty cells instead of iterating through the entire board.
These changes substantially reduced execution time.

## What I Learned
Overall, I learned a lot while working on this project in a relatively short period of time.

From bitwise operations and native processor instructions to profiling and designing meaningful benchmarks, 
this project helped me deepen my understanding of C++ performance.

It also gave me a better understanding of recursion, the function call stack, and the trade-offs involved in algorithm design and optimization.
