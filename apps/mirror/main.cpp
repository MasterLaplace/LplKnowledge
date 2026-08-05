/**
 * @file main.cpp
 * @brief Build the offline documentation mirror.
 *
 * The first vertical slice of the whole stack, on a corpus small enough to
 * finish: acquisition, streamed parsing, taxonomy, rendering, search. Also
 * immediately useful, because kernel work happens on machines that are
 * deliberately offline.
 *
 * The body below is the API written from the caller's side, before the callee
 * exists — the cheapest way to find out whether a library is pleasant to use. It
 * is fenced out until the modules it names are implemented, and the entry point
 * fails loudly rather than returning success it has not earned.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/Foundation.hpp>

#include <lpl/mirror/Dump.hpp>
#include <lpl/mirror/LogicalView.hpp>
#include <lpl/mirror/Render.hpp>
#include <lpl/mirror/Taxonomy.hpp>

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

#if 0 // ── intended usage ─────────────────────────────────────────────────────
    // One atomic archive instead of thousands of requests: temporally consistent,
    // polite, and re-runnable offline as often as the tooling needs.
    const lpl::mirror::Dump dump = lpl::mirror::Dump::open(argv[1]);

    lpl::mirror::Taxonomy taxonomy;
    lpl::mirror::LogicalView view{argv[2]};

    // Streamed: never hold more than one page. This has to run on a small machine.
    for (const lpl::mirror::Page &page : dump.pages(lpl::mirror::Namespace::Article))
    {
        const auto rendered = lpl::mirror::Render::toHtml(page);
        const auto stored = view.storeOnce(page.title(), rendered);

        // Categories are a cyclic graph and a page belongs to several at once.
        // Storage stays flat and unique; the tree is built from links into it.
        for (const auto &category : taxonomy.categoriesOf(page))
            view.link(category, stored);
    }

    view.writeIndex();
    lpl::core::log::info("mirror: {} pages, {} categories", view.pageCount(), view.categoryCount());
    return 0;
#endif // ─────────────────────────────────────────────────────────────────────

    LPL_NOT_IMPLEMENTED("lpl-mirror");
}
