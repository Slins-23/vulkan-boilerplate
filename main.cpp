#include "Engine.h"

int main() {
	Engine engine;

	try {
		engine.Load();
	}
	catch (std::exception & e) {
		std::cout << "Could not load renderer. Error: " << e.what() << std::endl;
		return 0;
	}

	engine.Start();
	engine.Close();

	return 1;
}