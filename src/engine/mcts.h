#pragma once
#include "engine.h"

#include <vector>

inline int16_t wdl_to_cp(double wdl)
{
    if (wdl == 0.0)
        return -32000;

    if (wdl == 1.0)
        return 32000;

    return std::clamp((int)std::round(std::log(wdl / (1 - wdl)) * 400), -32000, 32000);
}

inline double cp_to_wdl(int32_t cp)
{
    if (cp > param::CHECKMATE)
        return 1.0;

    if (cp < -param::CHECKMATE)
        return 0.0;

    return 1.0 / (1 + std::exp(-(double)cp / 400.0));
}

struct mcts_node
{
    // win/nodes is the prob that the move led to winning
    double wins = 0;
    double nodes = 0;
    double prior = 0.5;
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

        return children.empty();
    }

    void expand(chess::Board &board, nnue *net)
    {
        if (is_terminal)
            return;
        
        assert(children.empty());

        if (board.isInsufficientMaterial() || board.isRepetition(1) || board.isHalfMoveDraw())
        {
            is_terminal = true;
            return;
        }

        // net->initialize(board);

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        for (auto &move : moves)
        {
            mcts_node child{};
            child.move = move;
            child.parent = this;

            // net->make_move(board, move);
            // // TODO: fix bucket
            // auto cp = net->evaluate(board.sideToMove() ^ 1, (board.occ().count() - 2) / 4);
            // child.prior = 1.0 - cp_to_wdl(cp);
            // net->unmake_move();

            children.push_back(child);
        }

        if (moves.empty())
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

    int16_t get_cp() const
    {
        double win_prob = wins / nodes;
        return wdl_to_cp(win_prob);
    }
};

class mcts_engine
{
    mcts_node root{};
    timer clock{};
    std::unique_ptr<nnue> network = std::make_unique<nnue>();

  public:
    mcts_engine()
    {
        network->incbin_load();
    }

  private:
    mcts_node *tree_policy_child(mcts_node *node) const
    {
        double best_ucb1 = std::numeric_limits<double>::min();
        mcts_node *selection = &node->children[0];
        for (auto &child : node->children)
        {
            if (child.nodes == 0)
                return &child;

            double q = child.wins / child.nodes;
            double u = std::sqrt(2.0) * std::sqrt(std::log(node->nodes) / child.nodes);
            double ucb1 = q + u;
            if (ucb1 > best_ucb1)
            {
                best_ucb1 = ucb1;
                selection = &child;
            }
        }

        return selection;
    }

    double evaluate_policy(chess::Board &state)
    {
        int bucket = (state.occ().count() - 2) / 4;
        network->initialize(state);
        int32_t cp = network->evaluate(state.sideToMove(), bucket);
        return cp_to_wdl(cp);
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
        int depth = 0;
        int last_depth = 0;
        auto control = param.time_control(0, reference.sideToMove());
        clock.start(control.opt_time);

        root = mcts_node{};

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
            int seldepth = 0;
            while (!node->is_leaf())
            {
                node = tree_policy_child(node);
                board.makeMove(node->move);
                seldepth += 1;
            }

            // expansion
            node->expand(board, network.get());
            if (!node->is_terminal)
            {
                node = tree_policy_child(node);
                board.makeMove(node->move);
                seldepth += 1;
                if (seldepth > depth)
                    depth = seldepth;
            }

            // simulation
            double win;
            if (node->is_terminal)
            {
                auto state = board.isGameOver(1).second;
                if (state == chess::GameResult::DRAW)
                    win = 0.5;
                else
                    win = 1.0;
            }
            else
            {
                win = 1.0 - evaluate_policy(board);
            }

            // propagate
            while (node->has_parent())
            {
                node->update(win);
                win = 1.0 - win;
                node = node->parent;
            }
            node->update(win);

            if (depth > last_depth)
            {
                last_depth = depth;
                if (verbose)
                {
                    const mcts_node *best_node = best_move();
                    std::cout << "info " << "depth " << depth << " nodes " << int64_t(root.nodes)
                              << " score cp " << best_node->get_cp() << " time " << clock.delta()
                              << " pv " << chess::uci::moveToUci(best_node->move) << std::endl;
                }
            }
        }

        // best move
        const mcts_node *best_node = best_move();
        return {{best_node->move}, depth, best_node->get_cp()};
    }
};