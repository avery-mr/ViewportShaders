// Studio clay. Lights live in view space, so orbiting does not swing them across the model.
// Nitrous fills the matrices from the semantics. ClayColor and Wrap come from the parameter block.

float4x4 World : World;
float4x4 WorldViewProjection : WorldViewProjection;
float4x4 WorldInverseTranspose : WorldInverseTranspose;
float4x4 View : View;

float4 ClayColor
<
    string UIName = "Clay Color";
> = { 0.76, 0.73, 0.68, 1.0 };

float Wrap
<
    string UIName = "Wrap";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = 0.4;

float Cavity
<
    string UIName = "Cavity";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = 0.6;

float Edge
<
    string UIName = "Edge";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = 0.35;

float MatcapStrength
<
    string UIName = "Matcap Strength";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = 0.0;

bool UseMatcap = false;

Texture2D MatcapTex;
SamplerState MatcapSamp
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VSIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct VSOut
{
    float4 Position : SV_Position;
    float3 ViewNormal : TEXCOORD0;
    float3 ViewPos : TEXCOORD1;
};

VSOut FormClayVS(VSIn input)
{
    VSOut output;
    output.Position = mul(float4(input.Position, 1.0), WorldViewProjection);

    // Inverse-transpose keeps the normal perpendicular to the surface after a non-uniform scale.
    // View then puts it in camera space, where the lights are defined.
    float3 worldNormal = mul(input.Normal, (float3x3)WorldInverseTranspose);
    output.ViewNormal = mul(worldNormal, (float3x3)View);

    float3 worldPos = mul(float4(input.Position, 1.0), World).xyz;
    output.ViewPos = mul(float4(worldPos, 1.0), View).xyz;
    return output;
}

// A Lambert term goes black at the terminator. Wrap keeps a fraction of the light past that line.
float WrapLight(float3 N, float3 L, float wrap)
{
    return saturate((dot(N, L) + wrap) / (1.0 + wrap));
}

// ddx/ddy are the change across one pixel. No UVs.
// Positive where the normal turns with the surface (a ridge). Negative in a recess.
// Dividing by the position change keeps a coarse triangle weaker than a dense one.
float3 ApplyCurvature(float3 color, float3 N, float3 P)
{
    float3 dPx = ddx(P);
    float3 dPy = ddy(P);
    float denom = dot(dPx, dPx) + dot(dPy, dPy) + 1e-6;
    float k = (dot(ddx(N), dPx) + dot(ddy(N), dPy)) / denom;
    // The silhouette's derivatives spike. N.z is 0 there, so that term fades them out.
    k *= saturate(N.z);

    float recess = saturate(-k * 2.0);
    float ridge = saturate(k * 2.0);
    color *= (1.0 - recess * Cavity);
    color += ridge * Edge * float3(0.55, 0.52, 0.48);
    return color;
}

float4 FormClayPS(VSOut input) : SV_Target
{
    float3 N = normalize(input.ViewNormal);
    // Camera looks down -Z. A surface facing the camera has a view-space normal of +Z.
    float3 V = float3(0.0, 0.0, 1.0);

    float3 keyDir = normalize(float3(0.4, 0.8, 0.6));
    float3 fillDir = normalize(float3(-0.7, -0.15, 0.5));
    float3 keyColor = float3(1.0, 0.95, 0.88);
    float3 fillColor = float3(0.75, 0.82, 0.95);
    float3 rimColor = float3(0.90, 0.92, 0.95);

    float key = WrapLight(N, keyDir, Wrap);
    float fill = WrapLight(N, fillDir, 0.5);
    // 1 at the silhouette, 0 when the surface faces the camera.
    float rim = pow(1.0 - saturate(dot(N, V)), 3.0);

    float3 H = normalize(keyDir + V);
    // Low exponent is a wide highlight. The small scale keeps the clay matte.
    float spec = pow(saturate(dot(N, H)), 8.0) * 0.08;

    float3 light = keyColor * key * 1.05 + fillColor * fill * 0.45 + rimColor * rim * 0.28;
    float3 color = saturate(ApplyCurvature(ClayColor.rgb * light, N, input.ViewPos) + spec);

    // +X is right, +Y is up. Texture V is down, so Y is flipped.
    // A camera-facing normal lands in the middle of the picture.
    float2 uv = float2(N.x, -N.y) * 0.5 + 0.5;
    float3 matcap = MatcapTex.Sample(MatcapSamp, uv).rgb;
    float mixAmt = UseMatcap * saturate(MatcapStrength);
    color = lerp(color, matcap, mixAmt);
    return float4(color, 1.0);
}

RasterizerState FormClayCull
{
    CullMode = Back;
};

DepthStencilState FormClayDepth
{
    DepthEnable = true;
    DepthWriteMask = All;
    DepthFunc = Less;
};

technique11 FormClay
<
    string Script = "Pass=p0;";
>
{
    pass p0
    <
        string Script = "Draw=geometry;";
    >
    {
        SetRasterizerState(FormClayCull);
        SetDepthStencilState(FormClayDepth, 0);
        SetVertexShader(CompileShader(vs_5_0, FormClayVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, FormClayPS()));
    }
}
