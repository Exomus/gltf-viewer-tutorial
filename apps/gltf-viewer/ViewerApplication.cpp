#include "ViewerApplication.hpp"

#include <iostream>
#include <numeric>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

#include "utils/cameras.hpp"
#include "utils/gltf.hpp"
#include "utils/images.hpp"

#include <stb_image_write.h>
#include <tiny_gltf.h>

void keyCallback(
    GLFWwindow *window, int key, int scancode, int action, int mods)
{
  if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) {
    glfwSetWindowShouldClose(window, 1);
  }
}

int ViewerApplication::run()
{
  // Loader shaders
  const auto glslProgram =
      compileProgram({m_ShadersRootPath / m_AppName / m_vertexShader,
          m_ShadersRootPath / m_AppName / m_fragmentShader});

  tinygltf::Model model;

  if (!loadGltfFile(model)) {
    return -1;
  }

  const auto modelViewProjMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewProjMatrix");
  const auto modelViewMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewMatrix");
  const auto normalMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uNormalMatrix");

  // Build projection matrix
  glm::vec3 boundingBoxMax;
  glm::vec3 boundingBoxMin;

  computeSceneBounds(model, boundingBoxMin, boundingBoxMax);

  auto diagonalVect = boundingBoxMax - boundingBoxMin;
  auto distance = glm::length(diagonalVect);
  auto maxDistance = distance > 0 ? distance : 100; // TODO use scene bounds instead to compute this
  const auto projMatrix =
      glm::perspective(70.f, float(m_nWindowWidth) / m_nWindowHeight,
          0.001f * maxDistance, 1.5f * maxDistance);

  // TODO Implement a new CameraController model and use it instead. Propose the
  // choice from the GUI
  FirstPersonCameraController cameraController{
      m_GLFWHandle.window(), 1.0f * maxDistance};
  if (m_hasUserCamera) {
    cameraController.setCamera(m_userCamera);
  } else {
    // TODO Use scene bounds to compute a better default camera

    const auto center = (boundingBoxMax + boundingBoxMin) / 2.f;
    const auto up = glm::vec3(0, 1, 0);
    const auto eye = diagonalVect.z > 0
                         ? center + diagonalVect
                         : center + 2.f * glm::cross(diagonalVect, up);

    cameraController.setCamera(Camera{eye, center, up});
  }

  const auto vertexBufferObjectList = createBufferObjects(model);

  std::vector<VaoRange> meshVaoRangeList;
  const auto vertexAttributeObjectList =
      createVertexArrayObjects(model, vertexBufferObjectList, meshVaoRangeList);

  // Setup OpenGL state for rendering
  glEnable(GL_DEPTH_TEST);
  glslProgram.use();

  // Lambda function to draw the scene
  const auto drawScene = [&](const Camera &camera) {
    glViewport(0, 0, m_nWindowWidth, m_nWindowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto viewMatrix = camera.getViewMatrix();

    // The recursive function that should draw a node
    // We use a std::function because a simple lambda cannot be recursive
    const std::function<void(int, const glm::mat4 &)> drawNode =
        [&](int nodeIdx, const glm::mat4 &parentMatrix) {
          // TODO The drawNode function
          auto node = model.nodes[nodeIdx];
          auto nodeModelMatrix =
              getLocalToWorldMatrix(model.nodes[nodeIdx], parentMatrix);
          if (node.mesh >= 0) {
            const auto modelViewMatrix = viewMatrix * nodeModelMatrix;
            const auto modelViewProjectionMatrix = projMatrix * modelViewMatrix;
            const auto normalMatrix =
                glm::transpose(glm::inverse(modelViewMatrix));

            glUniformMatrix4fv(modelViewMatrixLocation, 1, GL_FALSE,
                value_ptr(modelViewMatrix));
            glUniformMatrix4fv(modelViewProjMatrixLocation, 1, GL_FALSE,
                value_ptr(modelViewProjectionMatrix));
            glUniformMatrix4fv(
                normalMatrixLocation, 1, GL_FALSE, value_ptr(normalMatrix));

            const auto &mesh = model.meshes[node.mesh];
            const auto &vaoRange = meshVaoRangeList[node.mesh];

            for (uint primIdx = 0; primIdx < mesh.primitives.size();
                 primIdx++) {
              const auto &primitiveVao =
                  vertexAttributeObjectList[vaoRange.begin + primIdx];
              const auto &primitive = mesh.primitives[primIdx];

              glBindVertexArray(primitiveVao);
              if (primitive.indices >= 0) {
                const auto &accessor = model.accessors[primitive.indices];
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto byteOffset =
                    accessor.byteOffset + bufferView.byteOffset;
                glDrawElements(primitive.mode, GLsizei(accessor.count),
                    accessor.componentType, (const GLvoid *)byteOffset);
              } else {
                const auto accessorIdx = (*begin(primitive.attributes)).second;
                const auto &accessor = model.accessors[accessorIdx];
                glDrawArrays(primitive.mode, 0, GLsizei(accessor.count));
              }
            }
          }

          for (const auto &childIdx : node.children) {
            drawNode(childIdx, nodeModelMatrix);
          }
        };

    // Draw the scene referenced by gltf file
    if (model.defaultScene >= 0) {
      for (auto nodeId : model.scenes[model.defaultScene].nodes) {
        drawNode(nodeId, glm::mat4(1));
      }
    }
  };

  if (!m_OutputPath.empty()) {
    std::vector<unsigned char> pixels(m_nWindowWidth * m_nWindowHeight * 3);
    renderToImage(m_nWindowWidth, m_nWindowHeight, 3, pixels.data(),
        [&]() { drawScene(cameraController.getCamera()); });

    flipImageYAxis(m_nWindowWidth, m_nWindowHeight, 3, pixels.data());
    const auto strPath = m_OutputPath.string();
    stbi_write_png(
        strPath.c_str(), m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), 0);

    return 0;
  }
  // Loop until the user closes the window
  for (auto iterationCount = 0u; !m_GLFWHandle.shouldClose();
       ++iterationCount) {
    const auto seconds = glfwGetTime();

    const auto camera = cameraController.getCamera();
    drawScene(camera);

    // GUI code:
    imguiNewFrame();

    {
      ImGui::Begin("GUI");
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
          1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("eye: %.3f %.3f %.3f", camera.eye().x, camera.eye().y,
            camera.eye().z);
        ImGui::Text("center: %.3f %.3f %.3f", camera.center().x,
            camera.center().y, camera.center().z);
        ImGui::Text(
            "up: %.3f %.3f %.3f", camera.up().x, camera.up().y, camera.up().z);

        ImGui::Text("front: %.3f %.3f %.3f", camera.front().x, camera.front().y,
            camera.front().z);
        ImGui::Text("left: %.3f %.3f %.3f", camera.left().x, camera.left().y,
            camera.left().z);

        if (ImGui::Button("CLI camera args to clipboard")) {
          std::stringstream ss;
          ss << "--lookat " << camera.eye().x << "," << camera.eye().y << ","
             << camera.eye().z << "," << camera.center().x << ","
             << camera.center().y << "," << camera.center().z << ","
             << camera.up().x << "," << camera.up().y << "," << camera.up().z;
          const auto str = ss.str();
          glfwSetClipboardString(m_GLFWHandle.window(), str.c_str());
        }
      }
      ImGui::End();
    }

    imguiRenderFrame();

    glfwPollEvents(); // Poll for and process events

    auto ellapsedTime = glfwGetTime() - seconds;
    auto guiHasFocus =
        ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    if (!guiHasFocus) {
      cameraController.update(float(ellapsedTime));
    }

    m_GLFWHandle.swapBuffers(); // Swap front and back buffers
  }

  // TODO clean up allocated GL data

  return 0;
}

ViewerApplication::ViewerApplication(const fs::path &appPath, uint32_t width,
    uint32_t height, const fs::path &gltfFile,
    const std::vector<float> &lookatArgs, const std::string &vertexShader,
    const std::string &fragmentShader, const fs::path &output) :
    m_nWindowWidth(width),
    m_nWindowHeight(height),
    m_AppPath{appPath},
    m_AppName{m_AppPath.stem().string()},
    m_ImGuiIniFilename{m_AppName + ".imgui.ini"},
    m_ShadersRootPath{m_AppPath.parent_path() / "shaders"},
    m_gltfFilePath{gltfFile},
    m_OutputPath{output}
{
  if (!lookatArgs.empty()) {
    m_hasUserCamera = true;
    m_userCamera =
        Camera{glm::vec3(lookatArgs[0], lookatArgs[1], lookatArgs[2]),
            glm::vec3(lookatArgs[3], lookatArgs[4], lookatArgs[5]),
            glm::vec3(lookatArgs[6], lookatArgs[7], lookatArgs[8])};
  }

  if (!vertexShader.empty()) {
    m_vertexShader = vertexShader;
  }

  if (!fragmentShader.empty()) {
    m_fragmentShader = fragmentShader;
  }

  ImGui::GetIO().IniFilename =
      m_ImGuiIniFilename.c_str(); // At exit, ImGUI will store its windows
  // positions in this file

  glfwSetKeyCallback(m_GLFWHandle.window(), keyCallback);

  printGLVersion();
}

bool ViewerApplication::loadGltfFile(tinygltf::Model &model)
{

  using namespace tinygltf;

  TinyGLTF loader;
  std::string error;
  std::string warning;

  bool ret = loader.LoadASCIIFromFile(
      &model, &error, &warning, m_gltfFilePath.string());
  if (!warning.empty()) {
    std::cerr << "Warn: " << warning.c_str() << std::endl;
  }

  if (!error.empty()) {
    std::cerr << "Error: " << error.c_str() << std::endl;
  }

  if (!ret) {
    std::cerr << "Failed to parse glTF" << std::endl;
    return false;
  }
  return true;
}

std::vector<GLuint> ViewerApplication::createBufferObjects(
    const tinygltf::Model &model)
{
  std::vector<GLuint> vertexBufferObjectList(model.buffers.size(), 0);
  glGenBuffers(model.buffers.size(), vertexBufferObjectList.data());

  for (unsigned long bufferIdx = 0; bufferIdx < model.buffers.size();
       bufferIdx++) {
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObjectList[bufferIdx]);
    glBufferStorage(GL_ARRAY_BUFFER, model.buffers[bufferIdx].data.size(),
        model.buffers[bufferIdx].data.data(), 0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  return std::move(vertexBufferObjectList);
}

std::vector<GLuint> ViewerApplication::createVertexArrayObjects(
    const tinygltf::Model &model, const std::vector<GLuint> &bufferObjects,
    std::vector<VaoRange> &meshIndexToVaoRange)
{
  /*
   * Model contains meshes that contains primitives
   * We want a VAO by primitives but we need to get track of all the VAO in the
   * same mesh.
   */
  std::vector<GLuint> vertexArrayObjectList;

  meshIndexToVaoRange.resize(model.meshes.size());

  struct Attribute
  {
    const std::string NAME;
    const int INDEX;
  };

  std::vector<Attribute> attributeList;
  attributeList.push_back({"POSITION", 0});
  attributeList.push_back({"NORMAL", 1});
  attributeList.push_back({"TEXCOORD_0", 2});
  for (const auto &mesh : model.meshes) {
    const auto oldSize = vertexArrayObjectList.size();

    auto vaoRange = VaoRange{static_cast<GLsizei>(oldSize),
        static_cast<GLsizei>(mesh.primitives.size())};
    meshIndexToVaoRange.push_back(vaoRange);

    vertexArrayObjectList.resize(oldSize + mesh.primitives.size());

    glGenVertexArrays(vaoRange.count, &vertexArrayObjectList[vaoRange.begin]);
    for (unsigned long primitiveId = 0; primitiveId < mesh.primitives.size();
         primitiveId++) {
      auto primitiveVAO = vertexArrayObjectList[vaoRange.begin + primitiveId];
      auto primitive = mesh.primitives[primitiveId];

      glBindVertexArray(primitiveVAO);
      for (Attribute attribute : attributeList) {
        // I'm opening a scope because I want to reuse the variable iterator in
        // the code for NORMAL and TEXCOORD_0
        const auto iterator = primitive.attributes.find(attribute.NAME);
        if (iterator !=
            end(primitive
                    .attributes)) { // If "POSITION" has been found in the map
          // (*iterator).first is the key "POSITION", (*iterator).second is the
          // value, ie. the index of the accessor for this attribute
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          const auto bufferObject = bufferObjects[bufferIdx];

          glEnableVertexAttribArray(attribute.INDEX);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObject);
          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
          glVertexAttribPointer(attribute.INDEX, accessor.type,
              accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
              (const GLvoid *)byteOffset);
          // Remember size is obtained with accessor.type, type is obtained with
          // accessor.componentType. The stride is obtained in the bufferView,
          // normalized is always GL_FALSE, and pointer is the byteOffset (don't
          // forget the cast).
        }

        if (primitive.indices >=
            0) { // Setting up the Index buffer object if exists
          const auto accessorIdx = primitive.indices;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          assert(GL_ELEMENT_ARRAY_BUFFER == bufferView.target);
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
              bufferObjects[bufferIdx]); // Binding the index buffer to
          // GL_ELEMENT_ARRAY_BUFFER while the VAO
          // is bound is enough to tell OpenGL we
          // want to use that index buffer for that
          // VAO
        }
      }
    }
  }

  std::clog << "Number of VAOs: " << vertexArrayObjectList.size() << std::endl;

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  return vertexArrayObjectList;
}