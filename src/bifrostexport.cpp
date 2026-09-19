#include "bifrostexport.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace BinaryNinja;

namespace bifrost {

// ── Parsed representation ───────────────────────────────────────────────────

namespace {

struct Block
{
    uint64_t    leftAddr = 0, rightAddr = 0;
    std::string status;   // identical | changed | added | removed
};

struct Row
{
    std::string name, legacyStatus, algorithm;
    uint64_t    leftAddr = 0, rightAddr = 0;
    double      similarity = 1.0, confidence = 1.0;
    int         bbIdentical = 0, bbChanged = 0, bbAdded = 0, bbRemoved = 0;
    std::vector<Block> blocks;
};

struct Parsed
{
    std::string left, right, timestamp;
    int identical = 0, changed = 0, added = 0, removed = 0;
    std::vector<Row> rows;
};

// "matched" refines to identical/changed via similarity; added/removed pass through.
std::string displayStatus(const std::string& legacy, double sim)
{
    if (legacy == "added" || legacy == "removed" || legacy == "identical" || legacy == "changed")
        return legacy;
    return sim >= 0.999 ? "identical" : "changed";
}

std::string hexAddr(uint64_t a)
{
    if (!a) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(a));
    return buf;
}

std::string mstr(Ref<Metadata> m)
{
    return (m && m->IsString()) ? m->GetString() : std::string();
}
uint64_t muint(Ref<Metadata> m)
{
    return (m && m->IsUnsignedInteger()) ? m->GetUnsignedInteger() : 0;
}
double mdouble(Ref<Metadata> m, double dflt)
{
    return (m && m->IsDouble()) ? m->GetDouble() : dflt;
}

bool parse(Ref<Metadata> diff, Parsed& out)
{
    if (!diff || !diff->IsKeyValueStore())
        return false;

    out.left      = mstr(diff->Get("left"));
    out.right     = mstr(diff->Get("right"));
    out.timestamp = mstr(diff->Get("timestamp"));

    auto funcs = diff->Get("functions");
    if (!funcs || !funcs->IsArray())
        return false;

    for (auto& entry : funcs->GetArray())
    {
        if (!entry || !entry->IsKeyValueStore()) continue;

        Row r;
        r.name         = mstr(entry->Get("name"));
        r.legacyStatus = mstr(entry->Get("status"));
        r.leftAddr     = muint(entry->Get("leftAddr"));
        r.rightAddr    = muint(entry->Get("rightAddr"));
        r.similarity   = mdouble(entry->Get("similarity"), 1.0);
        r.confidence   = mdouble(entry->Get("confidence"), 1.0);
        r.algorithm    = mstr(entry->Get("algorithm"));
        r.bbIdentical  = static_cast<int>(muint(entry->Get("bbIdentical")));
        r.bbChanged    = static_cast<int>(muint(entry->Get("bbChanged")));
        r.bbAdded      = static_cast<int>(muint(entry->Get("bbAdded")));
        r.bbRemoved    = static_cast<int>(muint(entry->Get("bbRemoved")));

        if (auto blocks = entry->Get("blocks"); blocks && blocks->IsArray())
        {
            for (auto& b : blocks->GetArray())
            {
                if (!b || !b->IsKeyValueStore()) continue;
                Block blk;
                blk.leftAddr  = muint(b->Get("l"));
                blk.rightAddr = muint(b->Get("r"));
                blk.status    = mstr(b->Get("s"));
                r.blocks.push_back(std::move(blk));
            }
        }

        std::string ds = displayStatus(r.legacyStatus, r.similarity);
        if      (ds == "added")     ++out.added;
        else if (ds == "removed")   ++out.removed;
        else if (ds == "changed")   ++out.changed;
        else                        ++out.identical;

        out.rows.push_back(std::move(r));
    }
    return true;
}

// ── Escaping ────────────────────────────────────────────────────────────────

std::string jsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            }
            else o += c;
        }
    }
    return o;
}

std::string htmlEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&': o += "&amp;";  break;
        case '<': o += "&lt;";   break;
        case '>': o += "&gt;";   break;
        case '"': o += "&quot;"; break;
        default:  o += c;
        }
    }
    return o;
}

// ── Writers ─────────────────────────────────────────────────────────────────

void writeJson(const Parsed& p, std::ostream& os)
{
    auto q = [](const std::string& s) { return "\"" + jsonEscape(s) + "\""; };

    os << "{\n";
    os << "  \"left\": "      << q(p.left)      << ",\n";
    os << "  \"right\": "     << q(p.right)     << ",\n";
    os << "  \"timestamp\": " << q(p.timestamp) << ",\n";
    os << "  \"summary\": { \"identical\": " << p.identical
       << ", \"changed\": " << p.changed
       << ", \"added\": "   << p.added
       << ", \"removed\": " << p.removed << " },\n";
    os << "  \"functions\": [\n";

    for (size_t i = 0; i < p.rows.size(); ++i)
    {
        const Row& r = p.rows[i];
        os << "    {"
           << " \"name\": "       << q(r.name)
           << ", \"status\": "    << q(displayStatus(r.legacyStatus, r.similarity))
           << ", \"similarity\": " << r.similarity
           << ", \"confidence\": " << r.confidence
           << ", \"algorithm\": "  << q(r.algorithm)
           << ", \"leftAddr\": "   << q(hexAddr(r.leftAddr))
           << ", \"rightAddr\": "  << q(hexAddr(r.rightAddr))
           << ", \"bb\": { \"identical\": " << r.bbIdentical
           << ", \"changed\": " << r.bbChanged
           << ", \"added\": "   << r.bbAdded
           << ", \"removed\": " << r.bbRemoved << " } }";
        os << (i + 1 < p.rows.size() ? ",\n" : "\n");
    }

    os << "  ]\n}\n";
}

void writeHtml(const Parsed& p, std::ostream& os)
{
    auto e = [](const std::string& s) { return htmlEscape(s); };

    os << "<!doctype html>\n<html><head><meta charset=\"utf-8\">\n";
    os << "<title>Bifrost diff: " << e(p.left) << " vs " << e(p.right) << "</title>\n";
    os << "<style>\n"
          "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:2rem;color:#1c1c1c;background:#fff}\n"
          "h1{font-size:1.2rem}\n"
          ".meta{color:#666;font-size:.85rem;margin-bottom:1rem}\n"
          ".summary span{display:inline-block;margin-right:1rem;font-weight:600}\n"
          "table{border-collapse:collapse;width:100%;font-size:.85rem;margin-top:1rem}\n"
          "th,td{text-align:left;padding:.35rem .6rem;border-bottom:1px solid #eee}\n"
          "th{border-bottom:2px solid #ddd}\n"
          "td.addr,td.num{font-family:ui-monospace,Menlo,monospace}\n"
          ".identical{color:#555}.changed{color:#b8860b}.added{color:#2e8b2e}.removed{color:#c0392b}\n"
          "tr.changed{background:#fff8e8}tr.added{background:#eefaee}tr.removed{background:#fdeeee}\n"
          "</style>\n</head><body>\n";

    os << "<h1>Bifrost diff</h1>\n";
    os << "<div class=\"meta\"><b>" << e(p.left) << "</b> &rarr; <b>" << e(p.right) << "</b>";
    if (!p.timestamp.empty()) os << " &middot; " << e(p.timestamp);
    os << "</div>\n";

    os << "<div class=\"summary\">"
       << "<span class=\"identical\">" << p.identical << " identical</span>"
       << "<span class=\"changed\">"   << p.changed   << " changed</span>"
       << "<span class=\"added\">"     << p.added     << " added</span>"
       << "<span class=\"removed\">"   << p.removed   << " removed</span>"
       << "</div>\n";

    os << "<table>\n<thead><tr>"
          "<th>Status</th><th>Function</th><th>Sim</th><th>Conf</th>"
          "<th>Algorithm</th><th>Left</th><th>Right</th><th>Blocks (=/~/+/-)</th>"
          "</tr></thead>\n<tbody>\n";

    char sim[16], conf[16];
    for (const Row& r : p.rows)
    {
        std::string ds = displayStatus(r.legacyStatus, r.similarity);
        bool scored = (ds != "added" && ds != "removed");
        std::snprintf(sim,  sizeof(sim),  "%.2f", r.similarity);
        std::snprintf(conf, sizeof(conf), "%.2f", r.confidence);

        os << "<tr class=\"" << ds << "\">"
           << "<td class=\"" << ds << "\">" << ds << "</td>"
           << "<td>" << e(r.name) << "</td>"
           << "<td class=\"num\">" << (scored ? sim  : "—") << "</td>"
           << "<td class=\"num\">" << (scored ? conf : "—") << "</td>"
           << "<td>" << e(r.algorithm) << "</td>"
           << "<td class=\"addr\">" << (r.leftAddr  ? hexAddr(r.leftAddr)  : "—") << "</td>"
           << "<td class=\"addr\">" << (r.rightAddr ? hexAddr(r.rightAddr) : "—") << "</td>"
           << "<td class=\"num\">" << r.bbIdentical << "/" << r.bbChanged
           << "/" << r.bbAdded << "/" << r.bbRemoved << "</td>"
           << "</tr>\n";
    }

    os << "</tbody></table>\n</body></html>\n";
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

bool exportDiffJson(Ref<Metadata> diff, const std::string& path)
{
    Parsed p;
    if (!parse(diff, p)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    writeJson(p, f);
    return static_cast<bool>(f);
}

bool exportDiffHtml(Ref<Metadata> diff, const std::string& path)
{
    Parsed p;
    if (!parse(diff, p)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    writeHtml(p, f);
    return static_cast<bool>(f);
}

} // namespace bifrost
