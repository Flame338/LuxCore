#include "LuxCore.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    luxcore::Index index;
    index.add("note-1", "Shopping list", "milk, eggs, bread and butter");
    index.add("note-2", "Reading list", "a book about how Lucene ranks search results");
    index.add("note-3", "Trip planning", "book the train to Edinburgh and pack a raincoat");

    const std::string query = argc > 1 ? argv[1] : "shoping lis";

    std::cout << "Searching " << index.size() << " notes for \"" << query << "\"\n";
    const std::vector<luxcore::Hit> hits = index.search(query);
    if (hits.empty()) {
        std::cout << "  no results\n";
    }
    for (const luxcore::Hit& hit : hits) {
        std::cout << "  " << hit.key << "  " << *index.stored(hit.key, "title") << "\n";
    }
    return 0;
}
