#pragma once
#include <glm/glm.hpp>

namespace core::audio {

    class AudioListener {
    public:
        AudioListener() = default;
        ~AudioListener() = default;

        void setPosition(const glm::vec3& position);
        glm::vec3 getPosition() const;

        void setVelocity(const glm::vec3& velocity);
        glm::vec3 getVelocity() const;

        void setOrientation(const glm::vec3& forward, const glm::vec3& up);
        void getOrientation(glm::vec3& forward, glm::vec3& up) const;

        void setGain(float gain);
        float getGain() const;

        void update(const glm::vec3& position, const glm::vec3& forward,
                    const glm::vec3& up, const glm::vec3& velocity = glm::vec3(0.0f));

    private:
        glm::vec3 currentPosition{0.0f};
        glm::vec3 currentVelocity{0.0f};
        glm::vec3 currentForward{0.0f, 0.0f, -1.0f};
        glm::vec3 currentUp{0.0f, 1.0f, 0.0f};
    };

}
