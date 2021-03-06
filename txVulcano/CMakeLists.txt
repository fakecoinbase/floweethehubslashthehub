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

project (txVulcano)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# set the default log section (for logDebug() with no args) to the POS value
add_definitions(-DLOG_DEFAULT_SECTION=12000)

include_directories(${LIBSERVER_INCLUDES} ${LIBAPPUTILS_INCLUDES} ${CMAKE_BINARY_DIR}/include)

add_executable(txVulcano
    main.cpp
    TxVulcano.cpp
    Wallet.cpp
)

target_link_libraries(txVulcano
    flowee_server
    flowee_networkmanager
    flowee_apputils
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${Event_LIBRARIES}
    ${MINIUPNP_LIBRARY}
    Qt5::Core
)


install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/txVulcano DESTINATION bin)
