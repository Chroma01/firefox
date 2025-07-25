# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
from urllib.request import urlretrieve

REVISION = "54b7efe39472f031ee6e3eab3fd3d14175564581"

urlretrieve(
    f"https://raw.githubusercontent.com/WICG/sanitizer-api/{REVISION}/builtins/safe-default-configuration.json",
    "safe-default-configuration.json",
)
config = json.load(open("safe-default-configuration.json"))

# Make sure there are no unexpected keys (like removeAttributes)
assert set(config.keys()) == {"elements", "attributes", "comments", "dataAttributes"}

assert config["comments"] is False
assert config["dataAttributes"] is False


def attributes_list(attributes):
    result = []

    for attr in attributes:
        assert set(attr.keys()) == {"name", "namespace"}
        assert attr["namespace"] is None

        name = attr["name"].replace("-", "_")
        result.append("nsGkAtoms::" + name)

    # Should not have duplicate attributes
    assert len(result) == len(set(result))

    return result


xhtml_elements = []
mathml_elements = []
svg_elements = []
attributes = []

xhtml_element_with_attributes = []
mathml_element_with_attributes = []
svg_element_with_attributes = []

for element in config["elements"]:
    assert set(element.keys()) == {"name", "namespace", "attributes"}

    namespace = element["namespace"]
    atom = "nsGkAtoms::" + element["name"]

    element_attributes = []
    if element["attributes"]:
        assert len(element["attributes"])
        element_attributes.append("/* element */ " + atom)
        element_attributes.extend(attributes_list(element["attributes"]))
        element_attributes.append("nullptr")

    if namespace == "http://www.w3.org/1999/xhtml":
        xhtml_elements.append(atom)
        xhtml_element_with_attributes.extend(element_attributes)
    elif namespace == "http://www.w3.org/1998/Math/MathML":
        mathml_elements.append(atom)
        mathml_element_with_attributes.extend(element_attributes)
    elif namespace == "http://www.w3.org/2000/svg":
        svg_elements.append(atom)
        svg_element_with_attributes.extend(element_attributes)
    else:
        raise TypeError(f"unknown namespace: {namespace}")

# Should not have duplicate elements
assert len(set(xhtml_elements)) == len(xhtml_elements)
assert len(set(mathml_elements)) == len(mathml_elements)
assert len(set(svg_elements)) == len(svg_elements)


def create_list_body(l):
    return "\n".join(map(lambda atom: f"    {atom},", l))


xhtml_elements_body = create_list_body(xhtml_elements)
mathml_elements_body = create_list_body(mathml_elements)
svg_elements_body = create_list_body(svg_elements)

attributes_body = create_list_body(attributes_list(config["attributes"]))

xhtml_element_with_attributes_body = create_list_body(
    xhtml_element_with_attributes + ["/* sentinel */ nullptr"]
)
mathml_element_with_attributes_body = create_list_body(
    mathml_element_with_attributes + ["/* sentinel */ nullptr"]
)
svg_element_with_attributes_body = create_list_body(
    svg_element_with_attributes + ["/* sentinel */ nullptr"]
)

out = open("SanitizerDefaultConfig.h", "w")
out.write(
    f"""/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SanitizerDefaultConfig_h
#define mozilla_dom_SanitizerDefaultConfig_h

// This file is automatically generated by update-safe-default-configuration.py

#include "mozilla/dom/SanitizerTypes.h"
#include "SanitizerDefaultConfig.h"
#include "nsGkAtoms.h"

namespace mozilla::dom::sanitizer {{

constexpr nsStaticAtom* kDefaultHTMLElements[] = {{
    // clang-format off
{ xhtml_elements_body }
    // clang-format on
}};

constexpr nsStaticAtom* kDefaultMathMLElements[] = {{
    // clang-format off
{ mathml_elements_body }
    // clang-format on
}};

constexpr nsStaticAtom* kDefaultSVGElements[] = {{
    // clang-format off
{ svg_elements_body }
    // clang-format on
}};

constexpr nsStaticAtom* kDefaultAttributes[] = {{
    // clang-format off
{ attributes_body }
    // clang-format on
}};

// Data is encoded as: element, attributes..., nullptr
constexpr nsStaticAtom* kHTMLElementWithAttributes[] = {{
    // clang-format off
{ xhtml_element_with_attributes_body }
    // clang-format on
}};

constexpr nsStaticAtom* kMathMLElementWithAttributes[] = {{
    // clang-format off
{ mathml_element_with_attributes_body }
    // clang-format on
}};

constexpr nsStaticAtom* kSVGElementWithAttributes[] = {{
    // clang-format off
{ svg_element_with_attributes_body }
    // clang-format on
}};

}}  // namespace mozilla::dom::sanitizer

#endif  // mozilla_dom_SanitizerDefaultConfig_h
"""
)
