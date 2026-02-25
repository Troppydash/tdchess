#pragma once
#include "engine.h"

#include <vector>

inline int16_t wdl_to_cp(double wdl)
{
    return std::clamp((int)std::round(std::log(wdl / (1 - wdl)) * 400), -32000,
                         32000);
}

struct mcts_node
{
    double wins = 0;
    double nodes = 0;
    std::vector<mcts_node> children{};
    mcts_node *parent = nullptr;
    chess::Move move{};
    bool is_terminal = false;

    mcts_node()
    {
    }

    bool is_leaf() const
    {
        if (is_terminal)
            return true;

        for (auto &c : children)
        {
            if (c.nodes == 0)
                return true;
        }

        return false;
    }

    void expand(const chess::Board &board)
    {
        assert(children.empty());

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        for (auto &move : moves)
        {
            mcts_node child{};
            child.move = move;
            child.parent = this;
            children.push_back(child);
        }

        if (moves.empty() || board.isInsufficientMaterial() || board.isRepetition(1) ||
            board.isHalfMoveDraw())
        {
            is_terminal = true;
        }
    }

    void update(double win)
    {
        nodes += 1;
        wins += win;
    }

    bool has_parent()
    {
        return parent != nullptr;
    }

    int16_t get_cp()
    {
        // sigmoid(cp / 400) = wdl
        double win_prob = wins / nodes;
        return wdl_to_cp(win_prob);
    }
};

class mcts_engine
{
    mcts_node root;
    timer clock;

    mcts_node *tree_policy_child(mcts_node *node) const
    {
        double best_ucb1 = std::numeric_limits<double>::min();
        mcts_node *selection = &node->children[0];
        for (auto &child : node->children)
        {
            double w = child.wins;
            double n = std::max(0.01, child.nodes);
            double N = child.parent->nodes;
            assert(n > 0);
            assert(N > 0);
            double ucb1 = w / n + std::sqrt(2.0) * std::sqrt(std::log(N) / n);
            if (ucb1 > best_ucb1)
            {
                best_ucb1 = ucb1;
                selection = &child;
            }
        }

        return selection;
    }

    mcts_node *tree_policy_expand(mcts_node *node)
    {
        // try to pick a node that is not explored
        for (auto &child : node->children)
        {
            if (child.nodes == 0)
                return &child;
        }

        assert(false);
    }

    void simulation_policy_child(chess::Board &state)
    {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, state);

        // randomly select move
        chess::Move move = moves[rand() % moves.size()];
        state.makeMove(move);
    }

    const mcts_node *best_move() const
    {
        // best move
        const mcts_node *best_node = &root.children[0];
        for (auto &child : root.children)
        {
            if (child.nodes > best_node->nodes)
            {
                best_node = &child;
            }
        }
        return best_node;
    }

  public:
    search_result search(const chess::Board &reference, search_param &param, bool verbose = false)
    {
        auto control = param.time_control(0, reference.sideToMove());
        clock.start(control.opt_time);

        root = mcts_node{};
        root.expand(reference);

        // do mcts
        chess::Board board;
        int freq = 1 << 15;
        for (int i = 0;; ++i)
        {
            if (i % freq == 0)
            {
                clock.check();
                if (clock.is_stopped())
                    break;
            }

            assert(!root.has_parent());

            // selection
            board = reference;
            mcts_node *node = &root;
            while (!node->is_leaf())
            {
                node = tree_policy_child(node);
                board.makeMove(node->move);
            }

            // expansion
            if (!node->is_terminal)
            {
                node = tree_policy_expand(node);
                board.makeMove(node->move);
                node->expand(board);
            }

            chess::Color node_side = board.sideToMove();

            // simulation
            chess::GameResult result;
            while (true)
            {
                auto [_, b] = board.isGameOver(1);
                if (b != chess::GameResult::NONE)
                {
                    result = b;
                    break;
                }

                simulation_policy_child(board);
            }
            double win = 0.5;
            if (result == chess::GameResult::LOSE)
                win = 0.0;
            assert(result != chess::GameResult::WIN);

            // scale to last expansion, note that end_side loses
            chess::Color end_side = board.sideToMove();
            if (end_side != node_side)
                win = 1.0 - win;

            // propagate
            while (node->has_parent())
            {
                node->update(win);
                win = 1.0 - win;
                node = node->parent;
            }
            node->update(win);

            if (i % freq == 0)
            {
                if (verbose)
                {
                    const mcts_node *best_node = best_move();
                    std::cout << "info " << "nodes " << root.nodes << " score cp " << root.get_cp()
                              << " pv " << chess::uci::moveToUci(best_node->move) << std::endl;
                }
            }
        }

        // best move
        const mcts_node *best_node = best_move();
        return {{best_node->move}, 1, root.get_cp()};
    }
};