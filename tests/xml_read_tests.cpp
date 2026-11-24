#include "doctest.h"

#include "../src/model/XmlRead.hpp"

using namespace tessera;

TEST_CASE("xml: elements, attributes, text and entities") {
    xml::Node root;
    std::string error;
    REQUIRE(xml::parse("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- a project -->\n"
                       "<Project version='1.0'><Track name=\"Bass &amp; Keys\" volume=\"0.5\" on=\"true\"/>"
                       "<Notes><Note key=\"60\"/><Note key=\"62\"/></Notes>"
                       "<Title>Caf&#233; &lt;1&gt;<![CDATA[<raw>]]></Title></Project>",
                       root, &error));
    CHECK(root.name == "Project");
    REQUIRE(root.children.size() == 3);
    const xml::Node* track = root.child("Track");
    REQUIRE(track);
    CHECK(*track->attr("name") == "Bass & Keys");
    CHECK(track->number("volume") == 0.5);
    CHECK(track->flag("on"));
    CHECK(track->number("missing", 7) == 7);
    CHECK(root.child("Notes")->children.size() == 2);
    CHECK(root.child("Title")->text == "Caf\xC3\xA9 <1><raw>");
}

TEST_CASE("xml: broken documents fail with where") {
    xml::Node root;
    std::string error;
    CHECK_FALSE(xml::parse("<A><B></A>", root, &error));
    CHECK(error.find("line 1") != std::string::npos);
    CHECK_FALSE(xml::parse("<A x=1/>", root, &error));
    CHECK_FALSE(xml::parse("<A>\n<B>", root, &error));
    CHECK(error.find("line 2") != std::string::npos);
    CHECK_FALSE(xml::parse("", root, &error));
}
