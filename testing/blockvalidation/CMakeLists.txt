# This file is part of the Flowee project
# Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

project (test_blockvalidation)
include (testlib)

add_executable(test_blockvalidation
    blockvalidation_tests.cpp
)
target_link_libraries(test_blockvalidation
    flowee_testlib_server
    flowee_server
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BERKELEY_DB_LIBRARIES}
    ${MINIUPNP_LIBRARY}
)
add_test(NAME HUB_test_blockvalidation COMMAND test_blockvalidation)
