/*
Copyright (c) 2015 Daniel Selsam. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Authors: Daniel Selsam, Leonardo de Moura
*/
#include <string>
#include "util/sexpr/format.h"
#include "kernel/expr.h"
#include "kernel/instantiate.h"
#include "kernel/error_msgs.h"
#include "library/attribute_manager.h"
#include "library/constants.h"
#include "library/unification_hint.h"
#include "library/util.h"
#include "library/expr_lt.h"
#include "library/scoped_ext.h"
#include "library/type_context.h"

namespace lean {

/* Unification hints */

unification_hint::unification_hint(expr const & lhs, expr const & rhs, list<expr_pair> const & constraints, unsigned num_vars):
    m_lhs(lhs), m_rhs(rhs), m_constraints(constraints), m_num_vars(num_vars) {}

int unification_hint_cmp::operator()(unification_hint const & uh1, unification_hint const & uh2) const {
    if (uh1.get_lhs() != uh2.get_lhs()) {
        return expr_quick_cmp()(uh1.get_lhs(), uh2.get_lhs());
    } else if (uh1.get_rhs() != uh2.get_rhs()) {
        return expr_quick_cmp()(uh1.get_rhs(), uh2.get_rhs());
    } else {
        auto it1 = uh1.get_constraints().begin();
        auto it2 = uh2.get_constraints().begin();
        auto end1 = uh1.get_constraints().end();
        auto end2 = uh2.get_constraints().end();
        for (; it1 != end1 && it2 != end2; ++it1, ++it2) {
            if (unsigned cmp = expr_pair_quick_cmp()(*it1, *it2)) return cmp;
        }
        return 0;
    }
}

/* Environment extension */

struct unification_hint_state {
    unification_hints m_hints;
    name_map<unsigned> m_decl_names_to_prio; // Note: redundant but convenient

    void validate_type(expr const & decl_type) {
        expr type = decl_type;
        while (is_pi(type)) type = binding_body(type);
        if (!is_app_of(type, get_unification_hint_name(), 0)) {
            throw exception("invalid unification hint, must return element of type `unification hint`");
        }
    }

    void register_hint(environment const & env, name const & decl_name, expr const & value, unsigned priority) {
        m_decl_names_to_prio.insert(decl_name, priority);
        type_context _ctx(env, options(), transparency_mode::All);
        tmp_type_context ctx(_ctx);
        expr e_hint = value;
        unsigned num_vars = 0;
        buffer<expr> tmp_mvars;
        while (is_lambda(e_hint)) {
            expr d = instantiate_rev(binding_domain(e_hint), tmp_mvars.size(), tmp_mvars.data());
            tmp_mvars.push_back(ctx.mk_tmp_mvar(d));
            e_hint = binding_body(e_hint);
            num_vars++;
        }

        if (!is_app_of(e_hint, get_unification_hint_mk_name(), 2)) {
            throw exception("invalid unification hint, body must be application of 'unification_hint.mk' to two arguments");
        }

        // e_hint := unification_hint.mk pattern constraints
        expr e_pattern = app_arg(app_fn(e_hint));
        expr e_constraints = app_arg(e_hint);

        // pattern := unification_constraint.mk _ lhs rhs
        expr e_pattern_lhs = app_arg(app_fn(e_pattern));
        expr e_pattern_rhs = app_arg(e_pattern);

        expr e_pattern_lhs_fn = get_app_fn(e_pattern_lhs);
        expr e_pattern_rhs_fn = get_app_fn(e_pattern_rhs);

        if (!is_constant(e_pattern_lhs_fn) || !is_constant(e_pattern_rhs_fn)) {
            throw exception("invalid unification hint, the heads of both sides of pattern must be constants");
        }

        if (quick_cmp(const_name(e_pattern_lhs_fn), const_name(e_pattern_rhs_fn)) > 0) {
            swap(e_pattern_lhs_fn, e_pattern_rhs_fn);
            swap(e_pattern_lhs, e_pattern_rhs);
        }

        name_pair key = mk_pair(const_name(e_pattern_lhs_fn), const_name(e_pattern_rhs_fn));

        buffer<expr_pair> constraints;
        unsigned eqidx = 1;
        while (is_app_of(e_constraints, get_list_cons_name(), 3)) {
            // e_constraints := cons _ constraint rest
            expr e_constraint = app_arg(app_fn(e_constraints));
            expr e_constraint_lhs = app_arg(app_fn(e_constraint));
            expr e_constraint_rhs = app_arg(e_constraint);
            constraints.push_back(mk_pair(e_constraint_lhs, e_constraint_rhs));
            e_constraints = app_arg(e_constraints);

            if (!ctx.is_def_eq(instantiate_rev(e_constraint_lhs, tmp_mvars.size(), tmp_mvars.data()),
                               instantiate_rev(e_constraint_rhs, tmp_mvars.size(), tmp_mvars.data()))) {
                throw exception(sstream() << "invalid unification hint, failed to unify constraint #" << eqidx);
            }
            eqidx++;
        }

        if (!is_app_of(e_constraints, get_list_nil_name(), 1)) {
            throw exception("invalid unification hint, must provide list of constraints explicitly");
        }

        if (!ctx.is_def_eq(instantiate_rev(e_pattern_lhs, tmp_mvars.size(), tmp_mvars.data()),
                           instantiate_rev(e_pattern_rhs, tmp_mvars.size(), tmp_mvars.data()))) {
            throw exception("invalid unification hint, failed to unify pattern after unifying constraints");
        }

        unification_hint hint(e_pattern_lhs, e_pattern_rhs, to_list(constraints), num_vars);
        unification_hint_queue q;
        if (auto const & q_ptr = m_hints.find(key)) q = *q_ptr;
        q.insert(hint, priority);
        m_hints.insert(key, q);
    }
};

struct unification_hint_entry {
    name m_decl_name;
    unsigned m_priority;
    unification_hint_entry(name const & decl_name, unsigned priority):
        m_decl_name(decl_name), m_priority(priority) {}
};

struct unification_hint_config {
    typedef unification_hint_entry entry;
    typedef unification_hint_state state;

    static void add_entry(environment const & env, io_state const &, state & s, entry const & e) {
        declaration decl = env.get(e.m_decl_name);
        s.validate_type(decl.get_type());
        s.register_hint(env, e.m_decl_name, decl.get_value(), e.m_priority);
    }
    static const char * get_serialization_key() { return "UNIFICATION_HINT"; }
    static void  write_entry(serializer & s, entry const & e) {
        s << e.m_decl_name << e.m_priority;
    }
    static entry read_entry(deserializer & d) {
        name decl_name; unsigned prio;
        d >> decl_name >> prio;
        return entry(decl_name, prio);
    }
    static optional<unsigned> get_fingerprint(entry const & e) {
        return some(hash(e.m_decl_name.hash(), e.m_priority));
    }
};

typedef scoped_ext<unification_hint_config> unification_hint_ext;

environment add_unification_hint(environment const & env, io_state const & ios, name const & decl_name, unsigned prio,
                                 bool persistent) {
    if (!env.get(decl_name).is_definition())
        throw exception(sstream() << "invalid unification hint, '" << decl_name << "' must be a definition");
    return unification_hint_ext::add_entry(env, ios, unification_hint_entry(decl_name, prio), persistent);
}

unification_hints get_unification_hints(environment const & env) {
    return unification_hint_ext::get_state(env).m_hints;
}

void get_unification_hints(unification_hints const & hints, name const & n1, name const & n2, buffer<unification_hint> & uhints) {
    if (quick_cmp(n1, n2) > 0) {
        if (auto const * q_ptr = hints.find(mk_pair(n2, n1)))
            q_ptr->to_buffer(uhints);
    } else {
        if (auto const * q_ptr = hints.find(mk_pair(n1, n2)))
            q_ptr->to_buffer(uhints);
    }
}

void get_unification_hints(environment const & env, name const & n1, name const & n2, buffer<unification_hint> & uhints) {
    unification_hints hints = unification_hint_ext::get_state(env).m_hints;
    get_unification_hints(hints, n1, n2, uhints);
}

/* Pretty-printing */

// TODO(dhs): I may not be using all the formatting functions correctly.
format unification_hint::pp(unsigned prio, formatter const & fmt) const {
    format r;
    if (prio != LEAN_DEFAULT_PRIORITY)
        r += paren(format(prio)) + space();
    format r1 = fmt(get_lhs()) + space() + format("=?=") + pp_indent_expr(fmt, get_rhs());
    r1 += space() + lcurly();
    r += group(r1);
    bool first = true;
    for (expr_pair const & p : m_constraints) {
        if (first) {
            first = false;
        } else {
            r += comma() + space();
        }
        r += fmt(p.first) + space() + format("=?=") + space() + fmt(p.second);
    }
    r += rcurly();
    return r;
}

format pp_unification_hints(unification_hints const & hints, formatter const & fmt) {
    format r;
    r += format("unification hints") + colon() + line();
    hints.for_each([&](name_pair const & names, unification_hint_queue const & q) {
            q.for_each([&](unification_hint const & hint) {
                    r += lp() + format(names.first) + comma() + space() + format(names.second) + rp() + space();
                    r += hint.pp(*q.get_prio(hint), fmt) + line();
                });
        });
    return r;
}

void initialize_unification_hint() {
    unification_hint_ext::initialize();

    register_system_attribute(basic_attribute("unify", "unification hint", add_unification_hint));
}

void finalize_unification_hint() {
    unification_hint_ext::finalize();
}
}
