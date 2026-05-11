struct VSInput {
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

Sampler2D textures[];

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : TEXCOORD0;
    float3 vGrassNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 vGrassData : TEXCOORD3;
    float3 cameraPosition : TEXCOORD4;
    uint instanceID : TEXCOORD5;
    int textureIndex : TEXCOORD6;
};

struct Frame {
    float4x4 projection;
    float4x4 view;
    float windSpeed;
    float time;
    float fieldSize;
    float pad1;
    float4 cameraPosition;
};

struct Mesh {
    float segments;
    float vertices;
    int textureIndex;
    int patchyGrass; 

    float4x4 model;

    float3 BASE_COLOR;
    float pad1;

    float3 TIP_COLOR;
    float pad2;

    float3 PATCH_BASE_COLOR;
    float pad3;

    float3 PATCH_TIP_COLOR;
    float pad4;

    int clump;           
    float3 finalPadding; 
};
struct PushConstants {
    Frame* frameBDA;   
    Mesh* meshArray;
    uint meshIndex;
    int instanceCount;
};

float3 hash(float3 p) {
    p = float3(
        dot(p, float3(123.42342, 123.3213, 1323.312)),
        dot(p, float3(234.242, 2342.1231, 13.12312)),
             dot(p, float3(123.231, 113.1231, 134.3421))
    );

    return -1.0 + 2.0 * fract(sin(p) * 4564453.2344);
}

float2 murmurHas21(uint src) {
    const uint M = 0x5bd1e995u;
    uint2 h = uint2(1190494759u, 2147483647u);
    src *= M;
    src ^= src >> 24u;
    src *= M;
    h *= M;
    h ^= src;
    h ^= h >> 13u;
    h *= M;
    h ^= h >> 15u;

    return h;
}

float2 hash21(float src) {
    float2 h = murmurHas21(asuint(src));
    return asfloat(h & 0x007fffffu | 0x3f800000u) - 1.0;
}

// functions utilized for rotation operations
float3x3 rotateY(float theta) {
    float c = cos(theta);
    float s = sin(theta);

    return float3x3( 
        float3(c, 0, s),
        float3(0, 1, 0),
        float3(-s, 0, c)
    );
}

float3x3 rotateAxis(float3 axis, float angle) {
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    return float3x3(
        oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
                oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s,
                oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
    );
}

// Helper functions
float inverseLerp(float v, float minValue, float maxValue) {
    return (v - minValue) / (maxValue - minValue);
}

float remap(float v, float inMin, float inMax, float outMin, float outMax) {
    float t = inverseLerp(v, inMin, inMax);
    return lerp(outMin, outMax, t);
}

// Leaf Shaping Function
float easeOut(float x, float t) {
    return 1.0 - pow(1.0 - x, t);
}

// Functions to handle Bezier Curve
float3 bezier(float3 P0, float3 P1, float3 P2, float3 P3, float t) {
    return (1.0 - t) * (1.0 - t) * (1.0 - t) * P0 +
        3.0 * (1.0 - t) * (1.0 - t) * t * P1 +
           3.0 * (1.0 - t) * t * t * P2 +
        t * t * t * P3;
}

float3 bezierGrad(float3 P0, float3 P1, float3 P2, float3 P3, float t) {
    return 3.0 * (1.0 - t) * (1.0 - t) * (P1 - P0) +
    6.0 * (1.0 - t) * t * (P2 - P1) +
    3.0 * t * t * (P3 - P2);
}

// Custom noise generation function
float noise(in float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);

    float3 u = f * f * (3.0 - 2.0 * f);

    return lerp(
        lerp(
            lerp(dot(hash(i + float3(0.0, 0.0, 0.0)), f - float3(0.0, 0.0, 0.0)),
                          dot(hash(i + float3(1.0, 0.0, 0.0)), f - float3(1.0, 0.0, 0.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 0.0)), f - float3(0.0, 1.0, 0.0)),
                          dot(hash(i + float3(1.0, 1.0, 0.0)), f - float3(1.0, 1.0, 0.0)), u.x),
                     u.y),
                lerp(
            lerp(dot(hash(i + float3(0.0, 0.0, 1.0)), f - float3(0.0, 0.0, 1.0)),
                          dot(hash(i + float3(1.0, 0.0, 1.0)), f - float3(1.0, 0.0, 1.0)), u.x),
                     lerp(dot(hash(i + float3(0.0, 1.0, 1.0)), f - float3(0.0, 1.0, 1.0)),
                       dot(hash(i + float3(1.0, 1.0, 1.0)), f - float3(1.0, 1.0, 1.0)), u.x),
                   u.y),
               u.z);
}

[shader("vertex")]
VSOutput main(VSInput input, uniform PushConstants pc ) {
    VSOutput output;

    Mesh currentMesh = pc.meshArray[pc.meshIndex];
    Frame frame = *pc.frameBDA;
    float windSpeed = frame.windSpeed;
    float time = frame.time;
    float windControl = windSpeed * 0.15;
    float fieldSize = frame.fieldSize;
    int globalInstanceCount = pc.instanceCount;

    float3 BASE_COLOR = currentMesh.BASE_COLOR;
    float3 TIP_COLOR = currentMesh.TIP_COLOR;
    float3 PATCH_BASE_COLOR = currentMesh.PATCH_BASE_COLOR;
    float3 PATCH_TIP_COLOR = currentMesh.PATCH_TIP_COLOR;
    bool patchyGrass = currentMesh.patchyGrass;
    bool clump = currentMesh.clump;

    float4x4 model = currentMesh.model;
    const float GRASS_VERTICES = currentMesh.vertices;
    const float GRASS_SEGMENTS = currentMesh.segments;

    int vertFB_ID = input.vertexID % (GRASS_VERTICES * 2);
    int vertID = vertFB_ID % GRASS_VERTICES;

    int xTest = vertID & 0x1;
    int zTest = (vertFB_ID >= GRASS_VERTICES) ? 1 : -1;

    float xSide = float(xTest);
    float zSide = float(zTest);
    float heightPercent = float(vertID - xTest) / (float(GRASS_SEGMENTS) * 2.0);

    uint width1; uint height1;
    textures[currentMesh.textureIndex].GetDimensions(width1, height1);


    // use the height and shape the width
    float height = 0.9;
    float width = 0.2;
    if (currentMesh.textureIndex == -1) {
        width = width * easeOut( 1.0 - heightPercent, 4.0 );

        //good formula for leaves
        //float x = saturate(heightPercent);
        //float base = x * (1.0 - x) * 1.0;

        //width = pow(base, 0.75);
    }
    else {
        width = 1.0;
        height = 1.5f;
    }

    float x = (xSide - 0.5) * width;
    float y = heightPercent * height;
    float z = 0.0;

    // offsetting each instance
    float2 hashedInstanceID = hash21(float( globalInstanceCount + input.instanceID ));
    float3 grassOffset = float3(hashedInstanceID.x, 0, hashedInstanceID.y) * fieldSize;

    // generating world position of grass
    float3 grassWorldPosition = mul( model, float4(grassOffset, 1.0)).xyz;

    float variation = remap(clamp(noise(grassWorldPosition), -1.0, 1.0), -1.0, 1.0, 0.0, 1.0);
     if( variation <0.5){
         width = 0.03;
         width = width * easeOut( 1.0 - heightPercent, 4.0 );
    }


    // height Variation
    float heightNoise = remap(noise(grassWorldPosition * 1.0), -1.0, 1.0, 0.8, 1.2);
    height *= heightNoise;

    if (clump) {
        float n = noise(grassWorldPosition * 0.3) * 0.5 + 0.5;
        float clumpFactor = smoothstep(0.1, 0.9, n); // Wider range

        if (clumpFactor < 0.7) {
            height = 0;
            y = 0;
            x = 0;
            width = 0;
        }
   }


    // hashing world position for random value
    float3 hashVal = hash(grassWorldPosition);

    // Bezier curve for animation bending
    float windAngle = 0.5;
    float windStrength = noise(float3(grassWorldPosition.xz * 0.05, 0.0) + time * windControl);

    float4 uv = float4(x, heightPercent, xSide, 0.0);

    float3 windAxis = float3(cos(windAngle), 0.0, sin(windAngle));
    float windLeanAngle = windStrength * 1.5 * heightPercent;

    float wind = noise(float3(grassWorldPosition.xz, time * windControl)) * (windStrength * 0.5 + 0.125);
    float leanFactor = (remap(hashVal.y, -1.0, 1.0, -0.5, 0.5) + wind) * (windControl > 0.0 ? 1.0 : 0.0);

    float3 p1 = float3(0.0);
    float3 p2 = float3(0.0, 0.22, 0.0);
    float3 p3 = float3(0.0, 0.66, 0.0);
    float3 p4 = float3(0.0, cos(leanFactor), sin(leanFactor));

    float3 curve = bezier(p1, p2, p3, p4, heightPercent);

    y = curve.y * height;
    z = curve.z * height;

    // generating rotation matrix
    const float PI = 3.14159;
    float angle = remap(hashVal.x, -1.0, 1.0, -PI, PI);

    //float3x3 grassMatrix = rotateAxis(windAxis, windLeanAngle) * rotateY(angle);
    float3x3 grassMatrix = mul(rotateAxis(windAxis, windLeanAngle), rotateY(angle));

    // generating normals for lighting
    float3 curveGrad = bezierGrad(p1, p2, p3, p4, heightPercent);
    float2x2 curveRot90 = float2x2(0.0, 1.0, -1.0, 0.0) * -zSide;

    float3 grassLocalNormal = mul( grassMatrix, float3(0.0, mul( curveRot90, curveGrad.yz )));

    float3 c1 = lerp(BASE_COLOR, TIP_COLOR, heightPercent);
    float3 c2 = lerp(PATCH_BASE_COLOR, PATCH_TIP_COLOR, heightPercent);
    float noiseValue = noise(grassWorldPosition * 0.4);

    float3 grassLocalPosition = mul( grassMatrix, float3(x, y, z)) + grassOffset;
    output.position = mul(frame.projection, mul(frame.view, mul(currentMesh.model, float4(grassLocalPosition, 1.0))));

    if (patchyGrass) {
        output.color = lerp(c2, c1, smoothstep(-0.5, 0.3, noiseValue));
    }
    else {
        output.color = lerp(BASE_COLOR, TIP_COLOR, heightPercent);
    }
    
    output.vGrassData = float4(x, heightPercent, xSide, 0.0);
    output.textureIndex = currentMesh.textureIndex;

    return output;
}

float3 phongSpecular(float3 normal, float3 lightDirection, float3 viewDirection) {
    float dotNL = saturate(dot(normal, lightDirection));

    float3 r = normalize(reflect(-lightDirection, normal));
    float phongValue = max(0.0, dot(viewDirection, r));
    phongValue = pow(phongValue, 32.0);

    float3 specular = dotNL * float3(phongValue);
    return specular;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_Target {

    float3 finalColor;
    
    float2 uv = 1.0 - input.vGrassData.zy;
    if (input.textureIndex == -1) {
        float grassX = input.vGrassData.x;
        finalColor = lerp(input.color * 0.75, input.color, smoothstep(0.125, 0.0, abs(grassX)));

    }
    else {
        float4 sampledColor = textures[input.textureIndex].Sample(uv).rgbw;
        if (sampledColor.w < 0.5) discard;
        finalColor = sampledColor.rgb;
    }

    return float4(finalColor.rgb, 1.0);
}

struct SkyOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
SkyOutput vs_sky(uint vertexID: SV_VertexID) {
    SkyOutput output;
    // This trick generates a triangle that covers the whole screen
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 1.0f, 1.0f);
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

[shader("fragment")]
float4 ps_sky(SkyOutput input) : SV_Target {

    float2 uv = input.uv;

    float3 topColor = float3(0.15, 0.25, 0.45);    // dusky blue
    float3 midColor = float3(0.95, 0.55, 0.25);    // orange
    float3 bottomColor = float3(0.35, 0.50, 0.20); // meadow green

    float t = uv.y;

    float3 skyColor;
    if (t < 0.5) {
        skyColor = lerp(bottomColor, midColor, t * 2.0);
    } else {
        skyColor = lerp(midColor, topColor, (t - 0.5) * 2.0);
    }

    float falloff = pow(1.0 - uv.y, 2.0);
    skyColor *= lerp(1.0, 0.75, falloff);

    float n = frac(sin(dot(uv * 200.0, float2(12.9898, 78.233))) * 43758.5453);
    skyColor += (n - 0.5) * 0.015;

    float luminance = dot(skyColor, float3(0.299, 0.587, 0.114));
    float desat = smoothstep(0.0, 0.4, uv.y);
    skyColor = lerp(float3(luminance, luminance, luminance), skyColor, desat);

    float2 sunPos = float2(0.7, 0.8);
    float dist = length(uv - sunPos);

    float sunCore = smoothstep(0.03, 0.0, dist);

    float sunGlow = smoothstep(0.25, 0.0, dist);

    float3 sunColor = float3(1.0, 0.85, 0.55);

    skyColor += sunColor * sunCore * 2.5; // intense core
    skyColor += sunColor * sunGlow * 0.5; // soft spread

    float glare = exp(-abs(uv.y - sunPos.y) * 60.0) * smoothstep(0.4, 0.0, dist);
    skyColor += sunColor * glare * 0.25;

    return float4(skyColor, 1.0);
}