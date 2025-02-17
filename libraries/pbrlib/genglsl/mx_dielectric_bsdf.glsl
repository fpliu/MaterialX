#include "lib/mx_microfacet_specular.glsl"

void mx_dielectric_bsdf_reflection(vec3 L, vec3 V, vec3 P, float occlusion, float weight, vec3 tint, float ior, vec2 roughness, float thinfilm_thickness, float thinfilm_ior, vec3 N, vec3 X, int distribution, int scatter_mode, inout BSDF bsdf)
{
    if (scatter_mode == 1) // BSDF_T (skip reflection - transmission only)
    {
        return;
    }

    if (weight < M_FLOAT_EPS)
    {
        return;
    }

    N = mx_forward_facing_normal(N, V);

    X = normalize(X - dot(X, N) * N);
    vec3 Y = cross(N, X);
    vec3 H = normalize(L + V);

    float NdotL = clamp(dot(N, L), M_FLOAT_EPS, 1.0);
    float NdotV = clamp(dot(N, V), M_FLOAT_EPS, 1.0);
    float VdotH = clamp(dot(V, H), M_FLOAT_EPS, 1.0);

    vec2 safeAlpha = clamp(roughness, M_FLOAT_EPS, 1.0);
    float avgAlpha = mx_average_alpha(safeAlpha);
    vec3 Ht = vec3(dot(H, X), dot(H, Y), dot(H, N));

    vec3 safeTint = max(tint, 0.0);
    FresnelData fd = mx_init_fresnel_dielectric(ior, thinfilm_thickness, thinfilm_ior);
    vec3  F = mx_compute_fresnel(VdotH, fd);
    float D = mx_ggx_NDF(Ht, safeAlpha);
    float G = mx_ggx_smith_G2(NdotL, NdotV, avgAlpha);

    float F0 = mx_ior_to_f0(ior);
    vec3 comp = mx_ggx_energy_compensation(NdotV, avgAlpha, F);
    vec3 dirAlbedo = mx_ggx_dir_albedo(NdotV, avgAlpha, F0, 1.0) * comp;
    bsdf.throughput = 1.0 - dirAlbedo * weight;

    // Note: NdotL is cancelled out
    bsdf.response = D * F * G * comp * safeTint * occlusion * weight / (4.0 * NdotV);
}

void mx_dielectric_bsdf_transmission(vec3 V, float weight, vec3 tint, float ior, vec2 roughness, float thinfilm_thickness, float thinfilm_ior, vec3 N, vec3 X, int distribution, int scatter_mode, inout BSDF bsdf)
{
    // Note: If scatter_mode is BSDF_R (reflection only) we must still keep evaluating both reflection/transmission
    // since reflection needs to attenuate the transmission amount in HW shaders when layering is used.

    if (weight < M_FLOAT_EPS)
    {
        return;
    }

    N = mx_forward_facing_normal(N, V);
    float NdotV = clamp(dot(N, V), M_FLOAT_EPS, 1.0);

    vec3 safeTint = max(tint, 0.0);
    FresnelData fd = mx_init_fresnel_dielectric(ior, thinfilm_thickness, thinfilm_ior);
    vec3 F = mx_compute_fresnel(NdotV, fd);

    vec2 safeAlpha = clamp(roughness, M_FLOAT_EPS, 1.0);
    float avgAlpha = mx_average_alpha(safeAlpha);

    float F0 = mx_ior_to_f0(ior);
    vec3 comp = mx_ggx_energy_compensation(NdotV, avgAlpha, F);
    vec3 dirAlbedo = mx_ggx_dir_albedo(NdotV, avgAlpha, F0, 1.0) * comp;
    bsdf.throughput = 1.0 - dirAlbedo * weight;

    if (scatter_mode != 0)
    {
        bsdf.response = mx_surface_transmission(N, V, X, safeAlpha, distribution, fd, safeTint) * weight;
    }
}

void mx_dielectric_bsdf_indirect(vec3 V, float weight, vec3 tint, float ior, vec2 roughness, float thinfilm_thickness, float thinfilm_ior, vec3 N, vec3 X, int distribution, int scatter_mode, inout BSDF bsdf)
{
    if (scatter_mode == 1) // BSDF_T (skip reflection - transmission only)
    {
        return;
    }

    if (weight < M_FLOAT_EPS)
    {
        return;
    }

    N = mx_forward_facing_normal(N, V);

    float NdotV = clamp(dot(N, V), M_FLOAT_EPS, 1.0);

    vec3 safeTint = max(tint, 0.0);
    FresnelData fd = mx_init_fresnel_dielectric(ior, thinfilm_thickness, thinfilm_ior);
    vec3 F = mx_compute_fresnel(NdotV, fd);

    vec2 safeAlpha = clamp(roughness, M_FLOAT_EPS, 1.0);
    float avgAlpha = mx_average_alpha(safeAlpha);

    float F0 = mx_ior_to_f0(ior);
    vec3 comp = mx_ggx_energy_compensation(NdotV, avgAlpha, F);
    vec3 dirAlbedo = mx_ggx_dir_albedo(NdotV, avgAlpha, F0, 1.0) * comp;
    bsdf.throughput = 1.0 - dirAlbedo * weight;

    vec3 Li = mx_environment_radiance(N, V, X, safeAlpha, distribution, fd);
    bsdf.response = Li * safeTint * comp * weight;
}

void mx_dielectric_bsdf(ClosureData closureData, float weight, vec3 tint, float ior, vec2 roughness, float thinfilm_thickness, float thinfilm_ior, vec3 N, vec3 X, int distribution, int scatter_mode, inout BSDF bsdf)
{
    if (closureData.closureType == CLOSURE_TYPE_REFLECTION)
    {
        mx_dielectric_bsdf_reflection(closureData.L, closureData.V, closureData.P, closureData.occlusion, weight, tint, ior, roughness, thinfilm_thickness, thinfilm_ior, N, X, distribution, scatter_mode, bsdf);
    }
    else if (closureData.closureType == CLOSURE_TYPE_TRANSMISSION)
    {
        mx_dielectric_bsdf_transmission(closureData.V, weight, tint, ior, roughness, thinfilm_thickness, thinfilm_ior, N, X, distribution, scatter_mode, bsdf);
    }
    else if (closureData.closureType == CLOSURE_TYPE_INDIRECT)
    {
        mx_dielectric_bsdf_indirect(closureData.V, weight, tint, ior, roughness, thinfilm_thickness, thinfilm_ior, N, X, distribution, scatter_mode, bsdf);
    }
}
