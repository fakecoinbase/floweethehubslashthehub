# This file is part of the Flowee project
# Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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

enable_testing()

add_subdirectory(test)
if (${Qt5Core_FOUND})
    find_package(Qt5Test)
    if (${Qt5Test_FOUND})
        add_subdirectory(common)
        add_subdirectory(prevector)
        add_subdirectory(utxo)
        add_subdirectory(doublespend)
        add_subdirectory(streaming)
        add_subdirectory(bitcoin-protocol)
        add_subdirectory(networkmanager)
        add_subdirectory(hashstorage)
        add_subdirectory(blockvalidation)
        add_subdirectory(api)

        find_package(Qt5Network)
        if (${Qt5Network_FOUND})
            add_subdirectory(httpengine)
            set (testHttp test_httpengine)
        endif ()

        add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} DEPENDS
            test_utxo
            test_doublespend
            test_protocol
            test_prevector
            test_hub
            test_streaming
            test_networkmanager
            test_hashstorage
            test_blockvalidation
            test_api
            ${testHttp}
        )
    else ()
        message("Missing qt5 (test) library, not building some tests")
    endif ()
endif()
