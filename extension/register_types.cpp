#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "character_bridge.hpp"
#include "move_order_bridge.hpp"

using namespace godot;

static void initialize_open_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(OpenMoveOrder);
	GDREGISTER_CLASS(OpenCharacter);
}

static void uninitialize_open_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT open_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
											 GDExtensionClassLibraryPtr p_library,
											 GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library, r_initialization);
	init_object.register_initializer(initialize_open_module);
	init_object.register_terminator(uninitialize_open_module);
	init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_object.init();
}
}
