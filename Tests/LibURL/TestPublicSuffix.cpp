/*
 * Copyright (c) 2025, Tim Ledbetter <tim.ledbetter@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <LibURL/PublicSuffixData.h>

TEST_CASE(is_public_suffix)
{
    EXPECT(URL::PublicSuffixData::is_public_suffix("com"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("com.br"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("co.uk"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("ac.uk"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("gov.uk"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("com.au"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("co.jp"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("公司.cn"sv));
    EXPECT(URL::PublicSuffixData::is_public_suffix("xn--55qx5d.cn"sv));

    EXPECT(!URL::PublicSuffixData::is_public_suffix(""sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("."sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix(".."sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("/"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("not-a-public-suffix.com"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("com."sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("com/"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("/com"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("not-a-public-suffix"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix(" com"sv));
    EXPECT(!URL::PublicSuffixData::is_public_suffix("com "sv));
}

TEST_CASE(get_public_suffix)
{
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix(""sv), OptionalNone {});
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("."sv), OptionalNone {});
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix(".."sv), OptionalNone {});
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix(" "sv), OptionalNone {});
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("/"sv), OptionalNone {});
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("not-a-public-suffix"sv), OptionalNone {});

    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("com"sv), "com"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("not-a-public-suffix.com"sv), "com"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("com."sv), "com"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix(".com."sv), "com"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("..com."sv), "com"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("com.br"sv), "com.br"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("not-a-public-suffix.com.br"sv), "com.br"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("co.uk"sv), "co.uk"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("bbc.co.uk"sv), "co.uk"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("www.bbc.co.uk"sv), "co.uk"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("公司.cn"sv), "公司.cn"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("www.公司.cn"sv), "公司.cn"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("xn--55qx5d.cn"sv), "xn--55qx5d.cn"sv);
    EXPECT_EQ(URL::PublicSuffixData::get_public_suffix("www.xn--55qx5d.cn"sv), "xn--55qx5d.cn"sv);
}
