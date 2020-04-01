/*
 * Copyright (C) 2020 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MULTIPASS_MOCK_STANDARD_PATHS_H
#define MULTIPASS_MOCK_STANDARD_PATHS_H

#include <multipass/standard_paths.h>

#include <gmock/gmock.h>

namespace multipass::test
{
// This will automatically verify expectations set upon it at the end of each test
class MockStandardPaths : public StandardPaths
{
public:
    using StandardPaths::StandardPaths;

    static void mockit();
    static MockStandardPaths& mock_instance();
};
} // namespace multipass::test

#endif // MULTIPASS_MOCK_STANDARD_PATHS_H
