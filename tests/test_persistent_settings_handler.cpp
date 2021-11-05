/*
 * Copyright (C) 2019-2021 Canonical, Ltd.
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

#include "common.h"
#include "mock_file_ops.h"
#include "mock_singleton_helpers.h"

#include <src/utils/wrapped_qsettings.h>

#include <multipass/constants.h>
#include <multipass/exceptions/settings_exceptions.h>
#include <multipass/persistent_settings_handler.h>

#include <QString>

#include <cstdio>

namespace mp = multipass;
namespace mpt = mp::test;
using namespace testing;

namespace
{
class MockQSettings : public mp::WrappedQSettings
{
public:
    using WrappedQSettings::WrappedQSettings; // promote visibility
    MOCK_CONST_METHOD0(status, QSettings::Status());
    MOCK_CONST_METHOD0(fileName, QString());
    MOCK_CONST_METHOD2(value_impl, QVariant(const QString& key, const QVariant& default_value)); // promote visibility
    MOCK_METHOD1(setIniCodec, void(const char* codec_name));
    MOCK_METHOD0(sync, void());
    MOCK_METHOD2(setValue, void(const QString& key, const QVariant& value));
};

class MockQSettingsProvider : public mp::WrappedQSettingsFactory
{
public:
    using WrappedQSettingsFactory::WrappedQSettingsFactory;
    MOCK_CONST_METHOD2(make_wrapped_qsettings,
                       std::unique_ptr<mp::WrappedQSettings>(const QString&, QSettings::Format));

    MP_MOCK_SINGLETON_BOILERPLATE(MockQSettingsProvider, WrappedQSettingsFactory);
};

class TestPersistentSettingsHandler : public Test
{
public:
    void inject_mock_qsettings() // moves the mock, so call once only, after setting expectations
    {
        EXPECT_CALL(*mock_qsettings_provider, make_wrapped_qsettings(_, Eq(QSettings::IniFormat)))
            .WillOnce(Return(ByMove(std::move(mock_qsettings))));
    }

    void mock_unreadable_settings_file(const char* filename)
    {
        std::fstream fstream{};
        fstream.setstate(std::ios_base::failbit);

        EXPECT_CALL(*mock_file_ops, open(_, StrEq(filename), Eq(std::ios_base::in)))
            .WillOnce(DoAll(WithArg<0>([](auto& stream) { stream.setstate(std::ios_base::failbit); }),
                            Assign(&errno, EACCES)));

        EXPECT_CALL(*mock_qsettings, fileName).WillOnce(Return(filename));
    }

public:
    mpt::MockFileOps::GuardedMock mock_file_ops_injection = mpt::MockFileOps::inject<NiceMock>();
    mpt::MockFileOps* mock_file_ops = mock_file_ops_injection.first;
    MockQSettingsProvider::GuardedMock mock_qsettings_injection = MockQSettingsProvider::inject<StrictMock>(); /* strict
                                                to ensure that, other than explicitly injected, no QSettings are used */
    MockQSettingsProvider* mock_qsettings_provider = mock_qsettings_injection.first;
    std::unique_ptr<NiceMock<MockQSettings>> mock_qsettings = std::make_unique<NiceMock<MockQSettings>>();
    FILE fake_file;
};

TEST_F(TestPersistentSettingsHandler, getReadsUtf8)
{
    const auto key = "asdf";
    mp::PersistentSettingsHandler handler{"", {{key, ""}}}; // TODO@ricab try to extract this
    EXPECT_CALL(*mock_qsettings, setIniCodec(StrEq("UTF-8"))).Times(1);

    inject_mock_qsettings();

    handler.get(key);
}

TEST_F(TestPersistentSettingsHandler, setWritesUtf8)
{
    const auto key = "a.key";
    mp::PersistentSettingsHandler handler{"", {{key, ""}}};
    EXPECT_CALL(*mock_qsettings, setIniCodec(StrEq("UTF-8"))).Times(1);

    inject_mock_qsettings();

    handler.set(key, "a value");
}

TEST_F(TestPersistentSettingsHandler, getThrowsOnUnreadableFile)
{
    const auto key = "foo", filename = "/an/unreadable/file";
    mp::PersistentSettingsHandler handler{filename, {{key, ""}}};

    mock_unreadable_settings_file(filename);
    inject_mock_qsettings();

    MP_EXPECT_THROW_THAT(handler.get(key), mp::PersistentSettingsException,
                         mpt::match_what(AllOf(HasSubstr("read"), HasSubstr("access"))));
}

TEST_F(TestPersistentSettingsHandler, setThrowsOnUnreadableFile)
{
    const auto key = mp::mounts_key, val = "yes", filename = "unreadable";
    mp::PersistentSettingsHandler handler{filename, {{key, ""}}};

    mock_unreadable_settings_file(filename);
    inject_mock_qsettings();

    MP_EXPECT_THROW_THAT(handler.set(key, val), mp::PersistentSettingsException,
                         mpt::match_what(AllOf(HasSubstr("read"), HasSubstr("access"))));
}

using DescribedQSettingsStatus = std::tuple<QSettings::Status, std::string>;
struct TestPersistentSettingsReadWriteError : public TestPersistentSettingsHandler,
                                              public WithParamInterface<DescribedQSettingsStatus>
{
};

TEST_P(TestPersistentSettingsReadWriteError, getThrowsOnFileReadError)
{
    const auto& [status, desc] = GetParam();
    const auto key = "token";
    mp::PersistentSettingsHandler handler{"", {{key, ""}}};

    EXPECT_CALL(*mock_qsettings, status).WillOnce(Return(status));

    inject_mock_qsettings();

    MP_EXPECT_THROW_THAT(handler.get(key), mp::PersistentSettingsException,
                         mpt::match_what(AllOf(HasSubstr("read"), HasSubstr(desc))));
}

TEST_P(TestPersistentSettingsReadWriteError, setThrowsOnFileWriteError)
{
    const auto& [status, desc] = GetParam();
    const auto key = "blah";
    mp::PersistentSettingsHandler handler{"", {{key, ""}}};

    {
        InSequence seq;
        EXPECT_CALL(*mock_qsettings, sync).Times(1); // needs to flush to ensure failure to write
        EXPECT_CALL(*mock_qsettings, status).WillOnce(Return(status));
    }

    inject_mock_qsettings();

    MP_EXPECT_THROW_THAT(handler.set(key, "bleh"), mp::PersistentSettingsException,
                         mpt::match_what(AllOf(HasSubstr("write"), HasSubstr(desc))));
}

INSTANTIATE_TEST_SUITE_P(TestSettingsAllReadErrors, TestPersistentSettingsReadWriteError,
                         Values(DescribedQSettingsStatus{QSettings::FormatError, "format"},
                                DescribedQSettingsStatus{QSettings::AccessError, "access"}));

TEST_F(TestPersistentSettingsHandler, getReturnsRecordedSetting)
{
    const auto key = "choose.a.key", val = "asdf", default_ = "some default";
    mp::PersistentSettingsHandler handler{"", {{key, default_}}};

    EXPECT_CALL(*mock_qsettings, value_impl(Eq(key), _)).WillOnce(Return(val));

    inject_mock_qsettings();

    ASSERT_NE(val, default_);
    EXPECT_EQ(handler.get(key), QString{val});
}

// TODO@ricab test get default
// TODO@ricab test refuses get/set unknown key

TEST_F(TestPersistentSettingsHandler, setRecordsProvidedSetting)
{
    const auto key = "name.a.key", val = "and a value";
    mp::PersistentSettingsHandler handler{"", {{key, ""}}};
    EXPECT_CALL(*mock_qsettings, setValue(Eq(key), Eq(val))).Times(1);

    inject_mock_qsettings();

    ASSERT_NO_THROW(handler.set(key, val));
}

} // namespace
