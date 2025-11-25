#pragma once

namespace core {

struct Window;

class Detector {
public:
    virtual ~Detector() = default;
    virtual void detect(const Window& /*w*/) = 0;
};

} // namespace core
