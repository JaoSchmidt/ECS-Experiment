# ECS-Experiment
Experimenting a bit with the entity component system

## Importing

**With CMake:** Add this repo as a submodule and link against the `ECSExperiment` target.

```cmake
add_subdirectory(path/to/ECS-Experiment)
target_link_libraries(your_target PUBLIC ECSExperiment)
```

**Without CMake:** Clone this repo and add its `include/` directory to your compiler's include path (`-I`).

```bash
git clone https://github.com/JaoSchmidt/ECS-Experiment.git
g++ -std=c++20 -I ECS-Experiment/include ...
```
