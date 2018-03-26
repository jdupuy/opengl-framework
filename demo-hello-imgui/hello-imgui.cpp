
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);

// -----------------------------------------------------------------------------
void renderGui()
{
	ImGui::Begin("Window");
	{
		static float f = 0.0f;
		static int counter = 0;
		ImGui::Text("Hello, ImGui!");
		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);

		if (ImGui::Button("Button"))
			counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
	ImGui::End();

	ImGui::Render();
}

// -----------------------------------------------------------------------------
void
keyboardCallback(
	GLFWwindow* window,
	int key, int scancode, int action, int modsls
) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard)
		return;

	if (action == GLFW_PRESS) {
		switch (key) {
			case GLFW_KEY_ESCAPE:
				glfwSetWindowShouldClose(window, GL_TRUE);
			break;
			default: break;
		}
	}
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
		return;
}

void mouseMotionCallback(GLFWwindow* window, double x, double y)
{
	static double x0 = 0, y0 = 0;
	double dx = x - x0,
	       dy = y - y0;

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
		return;

	x0 = x;
	y0 = y;
}

void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
		return;
}

// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Create the Window
	LOG("Loading {Window-Main}\n");
	GLFWwindow* window = glfwCreateWindow(800, 600, "Hello Imgui", NULL, NULL);
	if (window == NULL) {
		LOG("=> Failure <=\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, &keyboardCallback);
	glfwSetCursorPosCallback(window, &mouseMotionCallback);
	glfwSetMouseButtonCallback(window, &mouseButtonCallback);
	glfwSetScrollCallback(window, &mouseScrollCallback);

	// Load OpenGL functions
	LOG("Loading {OpenGL}\n");
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		LOG("gladLoadGLLoader failed\n");
		return -1;
	}

	LOG("-- Begin -- Demo\n");
	try {
		ImGui::CreateContext();
		ImGui_ImplGlfwGL3_Init(window, false);
		ImGui::StyleColorsDark();

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			glClearColor(0.8, 0.8, 0.8, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplGlfwGL3_NewFrame();
			renderGui();
			ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
		}

		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
	} catch (std::exception& e) {
		LOG("%s", e.what());
		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
		LOG("(!) Demo Killed (!)\n");

		return EXIT_FAILURE;
	} catch (...) {
		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
		LOG("(!) Demo Killed (!)\n");

		return EXIT_FAILURE;
	}
	LOG("-- End -- Demo\n");


	return 0;
}

