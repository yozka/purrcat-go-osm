cmake_minimum_required(VERSION 3.21)

project(OsmDemo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
	Core
	Gui
	Quick
	QuickControls2
	Positioning
	Location
)

qt_standard_project_setup(REQUIRES 6.5)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(OsmDemo
	main.cpp
)

qt_add_qml_module(OsmDemo
	URI OsmDemo
	VERSION 1.0
	QML_FILES
		main.qml
)

set_target_properties(OsmDemo PROPERTIES
	MACOSX_BUNDLE TRUE
	WIN32_EXECUTABLE TRUE
	OUTPUT_NAME OsmDemo
	MACOSX_BUNDLE_BUNDLE_NAME "OsmDemo"
	MACOSX_BUNDLE_GUI_IDENTIFIER "com.purrcatgo.osmdemo"
	MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
	MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_link_libraries(OsmDemo PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Quick
	Qt6::QuickControls2
	Qt6::Positioning
	Qt6::Location
)
