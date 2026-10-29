#version 330 core

struct Material {
    vec3 specular;
    float shininess;
};

struct DirLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    vec3 color;
};

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

uniform vec3 viewPos;
uniform DirLight dirLight;
uniform Material material;
uniform sampler2D texture1;

uniform bool gLightingEnabled;

// toon shading attributes
uniform bool gCellShadingEnabled;
const float toon_color_levels = 3.0;
const float toon_scale_factor = 1.0 / toon_color_levels;
uniform float toon_bias = 0.26;

const float gamma = 2.2;

void CalcDirLight(DirLight light, vec3 normal, vec3 viewDir,
                  out vec3 ambient, out vec3 diffuse, out vec3 specular)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    spec *= step(0.0, diff);

    ambient  = light.ambient * light.color;
    diffuse  = light.diffuse * diff * light.color;
    specular = light.specular * spec * material.specular * light.color;
}

void main()
{
    vec3 texColor = texture(texture1, TexCoords).rgb;
    vec3 result;

    if (gLightingEnabled)
    {
        vec3 norm = normalize(Normal);
        vec3 viewDir = normalize(viewPos - FragPos);

        vec3 ambient, diffuse, specular;
        CalcDirLight(dirLight, norm, viewDir, ambient, diffuse, specular);

        if (gCellShadingEnabled)
        {
            float toon_intensity = dot(diffuse, vec3(0.299, 0.587, 0.114));
            toon_intensity = clamp(toon_intensity, 0.0, 1.0);

            float toon = ceil(pow(toon_intensity, toon_bias) * toon_color_levels) * toon_scale_factor;

            diffuse = (toon_intensity > 0.0) ? diffuse * (toon / toon_intensity) : vec3(0.0);
        }

        result = (ambient + diffuse + specular) * texColor;
    }
    else
    {
        result = texColor;
    }

    result = clamp(result, 0.0, 1.0);
    FragColor = vec4(pow(result, vec3(1.0 / gamma)), 1.0);
}