#pragma once

#include "Board.hpp"

#include <cstdint>
#include <algorithm>
#include <bit>
#include <random>
#include <numeric>

using cell = std::uint8_t;
using mask = std::uint16_t;

struct Solver
{
    // benchmarking tool
    uint64_t counter = 0;
    emptyCounter index;
    bool solve(Board &board)
    {
        ++counter;
        auto emptyCell = findBestCell(board);
        int index_ = emptyCell.first;
        mask mask_ = emptyCell.second;
        if (index_ == -2)
        {
            return false;
        }
        if (index_ == -1)
        {
            return true;
        }
        mask lowestBit;
        while (mask_ != 0)
        {
            lowestBit = mask_ & (-mask_);
            auto number = std::countr_zero(lowestBit);
            board.modifyCell(index_, number);
            if (solve(board))
            {
                return true;
            }
            board.clearCell(index_);
            mask_ = mask_ & (mask_ - 1);
        }
        return false;
    }

    std::pair<int, mask> findBestCell(Board &board)
    {
        int minCandidates = 10;
        int bestCell = -1;
        mask bestMask = 0;
        auto emptyHi = board.emptyIndex.hi;
        auto emptyLo = board.emptyIndex.lo;
        mask candidates = 0;
        int index = -1;

        if (!emptyHi && !emptyLo)
        {
            return {-1, 0};
        }

        while (emptyHi != 0 || emptyLo != 0)
        {
            if (emptyLo != 0)
            {
                index = std::countr_zero(emptyLo);
                auto masks = board.findRelevantMasks(index);
                candidates = full & ~(*masks[0] | *masks[1] | *masks[2]);
                std::uint8_t numCandidates = std::popcount(candidates);

                if (numCandidates == 0)
                {
                    return {-2, 0};
                }
                if (numCandidates == 1)
                {
                    return {index, candidates};
                }
                if (numCandidates < minCandidates)
                {
                    minCandidates = numCandidates;
                    bestCell = index;
                    bestMask = candidates;
                }
                emptyLo &= emptyLo - 1;
            }
            else if (emptyHi != 0)
            {
                index = std::countr_zero(emptyHi) + 64;
                auto masks = board.findRelevantMasks(index);
                candidates = full & ~(*masks[0] | *masks[1] | *masks[2]);
                std::uint8_t numCandidates = std::popcount(candidates);

                if (numCandidates == 0)
                {
                    return {-2, 0};
                }
                if (numCandidates == 1)
                {
                    return {index, candidates};
                }
                if (numCandidates < minCandidates)
                {
                    minCandidates = numCandidates;
                    bestCell = index;
                    bestMask = candidates;
                }
                emptyHi &= emptyHi - 1;
            }
        }

        if (bestCell == -1)
            return {-1, 0};

        return {bestCell, bestMask};
    }
};

struct Generator
{
    Solver solver;
    uint64_t counter = 0;
    uint64_t lastPuzzleNodes = 0;

    bool fillBoard(Board &board, std::mt19937 &gen)
    {
        auto emptyHi = board.emptyIndex.hi;
        auto emptyLo = board.emptyIndex.lo;
        if (emptyHi == 0 && emptyLo == 0)
            return true;

        int index = emptyLo ? std::countr_zero(emptyLo)
                            : std::countr_zero(emptyHi) + 64;

        auto masks = board.findRelevantMasks(index);
        mask candidates = full & ~(*masks[0] | *masks[1] | *masks[2]);

        while (candidates != 0)
        {
            const int numCandidates = std::popcount(candidates);
            std::uniform_int_distribution<int> dist(0, numCandidates - 1);
            int skip = dist(gen);

            mask pick = candidates;
            while (skip-- > 0)
                pick &= (pick - 1);
            const int chosenNumber = std::countr_zero(pick);

            board.modifyCell(index, chosenNumber);
            if (fillBoard(board, gen))
                return true;
            board.clearCell(index);

            candidates &= ~(1u << chosenNumber);
        }
        return false;
    }

    bool hasMultipleSolutions(Board &board, int &solutionsFound, int limit = 2)
    {
        ++counter;
        auto emptyCell = solver.findBestCell(board);
        int index = emptyCell.first;
        mask mask_ = emptyCell.second;
        if (index == -2)
            return false;
        if (index == -1)
        {
            ++solutionsFound;
            return solutionsFound >= limit;
        }
        while (mask_ != 0)
        {
            const int number = std::countr_zero(mask_);
            board.modifyCell(index, number);
            const bool found = hasMultipleSolutions(board, solutionsFound, limit);
            board.clearCell(index); // SIEMPRE, antes de devolver
            if (found)
                return true;
            mask_ &= mask_ - 1;
        }
        return false;
    }

    bool dig(Board &board, std::mt19937 &gen, int targetHoles)
    {
        std::array<std::uint8_t, 81> cells;
        std::iota(cells.begin(), cells.end(), 0);
        std::shuffle(cells.begin(), cells.end(), gen);

        int counter_ = 0;

        while (targetHoles > 0 && counter_ <= 80)
        {
            int solutions = 0;
            int limit = 2;
            int old = board.cells[cells[counter_]];
            board.clearCell(cells[counter_]);

            bool multiples = hasMultipleSolutions(board, solutions, limit);

            if (multiples || solutions != 1)
            {
                board.modifyCell(cells[counter_], old);
            }
            else
            {
                --targetHoles;
            }
            ++counter_;
        }
        return true;
    }

    bool generate(Board &board, std::mt19937 &gen, int targetNodes)
    {
        int actualNodes = 0;
        int attempts = 0;
        do
        {
            board.clearBoard();
            if (!fillBoard(board, gen))
            {
                ++attempts;
                continue;
            }
            dig(board, gen, 55);
            Board copy = board;
            solver.counter = 0;
            solver.solve(copy);
            actualNodes = solver.counter;
            ++attempts;
        } while (actualNodes < targetNodes && attempts < 100);
        lastPuzzleNodes = actualNodes;
        if (actualNodes >= targetNodes)
        {
            return true;
        }
        return false;
    }
};