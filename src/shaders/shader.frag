#version 430

layout(std430, binding=3) buffer FB {
    vec4 frameBuffer[];
};

layout(location=0) uniform ivec4 params;
// layout(location=1) uniform vec4 floatParams;

ivec2 res = params.xy;

// const float focalDistance = 5.;
// const float blurStrength = 3.;
// const float maxSigma = 4.;

out vec4 outCol;


int pixelIndex(ivec2 p) {
    return p.x + p.y * res.x;
}

// bool isInsideScreen(ivec2 p) {
//     return p.x >= 0 && p.y >= 0 && p.x < res.x && p.y < res.y;
// }

// float gaussianWeight(float r2, float s2) {
//     return exp(-0.5 * r2 / s2);
// }

void main()
{
    ivec2 uv = ivec2(gl_FragCoord.xy);
    vec4 center = frameBuffer[pixelIndex(uv)];
    vec3 col = center.xyz;

    // depth of field: gaussian blur based on distance at pixel
    /*float depth = center.w;
    float sigma = blurStrength * abs(depth - focalDistance) / max(depth, 0.01);
    sigma = clamp(sigma, 0., maxSigma);

    if (sigma > 0.01) {
        int r = int(ceil(2 * sigma));
        float weight = 0;
        col = vec3(0);
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                ivec2 duv = ivec2(dx, dy);
                ivec2 p = uv + duv;
                if (isInsideScreen(p)) {
                    float w = gaussianWeight(dot(duv,duv), sigma*sigma);
                    vec3 sampleCol = frameBuffer[pixelIndex(p)].xyz;
                    col += w * sampleCol;
                    weight += w;
                }
            }
        }
        col /= max(weight, 1e-3);
    }*/

    col = col / (1. + col); // tone mapping
    col = pow(col, vec3(1. / 2.2)); // gamma correction

    outCol = vec4(col, 1.);
}