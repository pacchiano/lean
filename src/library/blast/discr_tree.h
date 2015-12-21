/*
Copyright (c) 2015 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include "kernel/expr.h"
#include "library/expr_lt.h"

namespace lean {
namespace blast {
/**
   \brief (Imperfect) discrimination trees.

   The edges are labeled with:
   1- Constant names (the universes are ignored)
   2- Local names (e.g., hypotheses)
   3- Star/Wildcard (we use them to encode metavariables). We use the same symbol
      for all metavariables. Remark: in the discrimination tree literature, our
      metavariables are called variables.
   4- Unsupported. We use them to encode nested lambda's, Pi's, Sort's
      Anything that is not an application, constant or local.

   When indexing terms, we ignore propositions and instance implicit
   arguments. We use blast get_fun_info procedure for retrieving
   this information. Thus, this data-structure should only be used
   inside of the blast module. */
class discr_tree {
public:
    struct node_cell;
private:
    enum class edge_kind { Local, Constant, Star, Unsupported };
    struct edge;
    struct edge_cmp;
    struct node_cmp;
    struct node {
        node_cell * m_ptr;
        node():m_ptr(nullptr) {}
        node(node_cell * ptr);
        node(node const & s);
        node(node && s);

        ~node();
        node & operator=(node const & n);
        node & operator=(node&& n);
        operator bool() const { return m_ptr != nullptr; }
        bool is_shared() const;
        node steal() { node r; swap(r, *this); return r; }
        void trace(optional<edge> const & e, unsigned depth, bool disj) const;
        friend void swap(node & n1, node & n2) { std::swap(n1.m_ptr, n2.m_ptr); }
    };

    static node ensure_unshared(node && n);
    static node insert_atom(node && n, edge const & e, buffer<expr> & todo, expr const & v, buffer<pair<node, node>> & skip);
    static node insert_star(node && n, buffer<expr> & todo, expr const & v, buffer<pair<node, node>> & skip);
    static node insert_app(node && n, bool is_root, expr const & e, buffer<expr> & todo, expr const & v, buffer<pair<node, node>> & skip);
    static node insert(node && n, bool is_root, buffer<expr> & todo, expr const & v, buffer<pair<node, node>> & skip);

    node m_root;
public:
    void insert(expr const & k, expr const & v);
    void insert(expr const & k) { insert(k, k); }
    void erase(expr const & k, expr const & v);
    void erase(expr const & k) { erase(k, k); }

    void find(expr const & e, std::function<bool(expr const &)> const & fn) const;
    void collect(expr const & e, buffer<expr> & r) const;

    void trace() const;
};
void initialize_discr_tree();
void finalize_discr_tree();
}}
