#pragma once

#include <string>
#include <vector>

namespace ZmInternal {
    namespace Render {

        class Fonts {
        public:
            Fonts();
            ~Fonts();

            // Load fonts from memory or assets
            bool LoadDefaultFont();

            // Get font texture (for ImGui)
            // In a real implementation, this would return the ImFont*

        private:
            std::vector<unsigned char> m_fontData;
            bool m_fontLoaded = false;
        };

    } // namespace Render
} // namespace ZmInternal