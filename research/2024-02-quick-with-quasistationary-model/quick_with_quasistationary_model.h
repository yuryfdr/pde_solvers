﻿#pragma once

#include <pde_solvers/timeseries.h>

inline std::string prepare_research_folder_for_qsm_model(std::string dop_path = "")
{
    auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string research_name = std::string(test_info->test_case_name());
    std::string case_name = std::string(test_info->name());

    std::string path = std::string("../research_out/QSM_models/") +
        research_name + "/" + case_name + "/" + dop_path + "/";
    std::filesystem::create_directories(path);
    for (const auto& entry : filesystem::directory_iterator(path)) {
        filesystem::remove_all(entry.path());
    }
    return path;
}

/// @brief Тесты для солвера
class IsothermalQuasistaticModel : public ::testing::Test {
protected:
    /// @brief Параметры трубы
    pipe_properties_t pipe;
protected:

    /// @brief Подготовка к расчету для семейства тестов
    virtual void SetUp() override {
        // Упрощенное задание трубы - 200км, с шагом разбиения для расчтной сетки 100 м, диаметром 514мм
        simple_pipe_properties simple_pipe;
        simple_pipe.length = 200e3;
        simple_pipe.diameter = 0.514;
        simple_pipe.dx = 100;

        pipe = pipe_properties_t::build_simple_pipe(simple_pipe);
    }

public:
    /// @brief Создание профиля отличающегося от прямой линии
    /// Возрастает в первойи четвёртой четвертях, убывает во второй и в третьей
    vector<double> create_honest_profile() const
    {
        double z_min = 140;
        double z_max = 180;
        double z_start = (z_max + z_min) / 2;
        double dz = (z_max - z_start) / pipe.profile.getPointCount();

        vector<double> heights(pipe.profile.getPointCount(), z_start);

        const vector<double>& coordinates = pipe.profile.coordinates;
        for (size_t index = 1; index < heights.size(); index++)
        {
            size_t proc = 100 * index / heights.size();
            if (proc <= 25)
            {
                heights[index] = heights[index - 1] + dz;
            } 
            else if (proc <= 75) 
            {
                heights[index] = heights[index - 1] - 1.2 * dz;
            }
            else
            {
                heights[index] = heights[index - 1] + 1.8 * dz;
            }
        }
        
        return heights;
    }

    vector_timeseries_t generate_timeseries(const vector<pair<string, double>>& timeseries_initial_values, 
        timeseries_generator_settings settings = timeseries_generator_settings::default_settings(), 
        time_t jump_time = 0, double jump_value = 0, string name_parameter = "")
    {
        // Задаем время 04.08.2024  16:42:53
        settings.start_time = 1712583773;
        // Генерируем данные
        synthetic_time_series_generator data_time_series(timeseries_initial_values, settings);
        if (jump_time != 0 && name_parameter != "")
        {
            data_time_series.apply_jump(jump_time, jump_value, name_parameter);
        }
        // Получаем данные
        const auto& data = data_time_series.get_data();
        // Помещаем временные ряды в вектор
        vector_timeseries_t params(data);
        return params;
    }

    /// @brief Стационарный расчет (с помощью initial boundaries),
    /// а затем квазистационарный расчет по краевым условиям (boundary_timeseries)
    /// @tparam Layer Слой для расчета плотности, вязкости и давления для численного метода
    /// @tparam Solver Численный метод расчета движения партий
    /// @param path Путь к файлу с результатом
    /// @param initial_boundaries Начальные условия
    /// @param boundary_timeseries Краевые условия
    /// @param dt Шаг по времени либо задаётся постоянным, 
    /// либо рассчитывается на каждом шаге моделирования для Cr = 1
    template <typename Solver>
    void perform_quasistatic_simulation(
        const string& path, 
        const isothermal_quasistatic_task_boundaries_t& initial_boundaries,
        const vector_timeseries_t& boundary_timeseries, 
        double dt = std::numeric_limits<double>::quiet_NaN())
    {
        isothermal_quasistatic_task_t<Solver> task(pipe);
        task.solve(initial_boundaries);

        time_t t = boundary_timeseries.get_start_date(); // Момент времени начала моделирования

        // Печатаем профиль трубы и первый слой к нему
        task.print_profile(path);
        task.print_all(t, path);

        do
        {
            // Интерполируем значения параметров в заданный момент времени
            vector<double> values_in_time_model = boundary_timeseries(t);
            isothermal_quasistatic_task_boundaries_t boundaries(values_in_time_model);

            double time_step = dt;
            if (std::isnan(time_step)) {
                double v = boundaries.volumetric_flow / pipe.wall.getArea(); 
                time_step = task.get_time_step_assuming_max_speed(v);
            }
            t += static_cast<time_t>(time_step);

            task.step(time_step, boundaries);
            task.print_all(t, path);
        } while (t < boundary_timeseries.get_end_date());
    }
};

/// @brief Пример использования метода Quickest Ultimate с гидравлическим расчетом  
TEST_F(IsothermalQuasistaticModel, QuickWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();

    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Вызываем метод расчета квазистационарной модели с помощью Quickest Ultimate
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values);
    double dt = 75;
    perform_quasistatic_simulation<quickest_ultimate_fv_solver>(
        path, initial_boundaries, time_series, dt);
}
/// @brief Пример использования метода Quickest Ultimate с гидравлическим расчетом (идеальные настройки)
TEST_F(IsothermalQuasistaticModel, IdealQuickWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();

    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };

        // Объявляем структуру с исходными данными и настроечными параметрами
    timeseries_generator_settings settings = timeseries_generator_settings::default_settings();
    settings.value_relative_decrement = 0;
    settings.value_relative_increment = 0;
    settings.sample_time_max = 200;
    settings.sample_time_min = 200;
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values, settings);
    // Вызываем метод расчета квазистационарной модели с помощью Quickest Ultimate
    perform_quasistatic_simulation<quickest_ultimate_fv_solver>(
        path, initial_boundaries, time_series);
}
/// @brief Пример использования метода характеристик с гидравлическим расчетом  
TEST_F(IsothermalQuasistaticModel, MocWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();
    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Вызываем метод расчета квазистационарной модели с помощью МХ
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values);
    double dt = 75;
    perform_quasistatic_simulation<advection_moc_solver>(
        path, initial_boundaries, time_series, dt);
}
/// @brief Пример использования метода характеристик (переменный шаг) с гидравлическим расчетом  
TEST_F(IsothermalQuasistaticModel, OptionalStepMocWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();
    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Вызываем метод расчета квазистационарной модели с помощью МХ
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values);
    perform_quasistatic_simulation<advection_moc_solver>(
        path, initial_boundaries, time_series);
}

/// @brief Пример использования метода характеристик с гидравлическим расчетом (идеальные настройки)  
TEST_F(IsothermalQuasistaticModel, IdealMocWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();
    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Объявляем структуру с исходными данными и настроечными параметрами
    timeseries_generator_settings settings = timeseries_generator_settings::default_settings();
    settings.value_relative_decrement = 0;
    settings.value_relative_increment = 0;
    settings.sample_time_max = 200;
    settings.sample_time_min = 200;
    // Вызываем метод расчета квазистационарной модели с помощью МХ
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values, settings);
    perform_quasistatic_simulation<advection_moc_solver>(
        path, initial_boundaries, time_series);
}

/// @brief Пример использования метода характеристик с гидравлическим расчетом (идеальные настройки)
/// Рассматривается пример с импульсной партией нефти
TEST_F(IsothermalQuasistaticModel, IdealImpulsMocWithQuasiStationaryModel)
{
    // Создаём папку с результатами и получаем путь к ней
    string path = prepare_research_folder_for_qsm_model();
    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Объявляем структуру с исходными данными и настроечными параметрами
    timeseries_generator_settings settings = timeseries_generator_settings::default_settings();
    settings.value_relative_decrement = 0;
    settings.value_relative_increment = 0;
    settings.sample_time_max = 200;
    settings.sample_time_min = 200;
    time_t jump_time = 5000;
    double jump_value = -10;
    // Вызываем метод расчета квазистационарной модели с помощью МХ
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values, settings, jump_time, jump_value, "rho_in");
    perform_quasistatic_simulation<advection_moc_solver>(
        path, initial_boundaries, time_series);
}


/// @brief Наглядное влияние выбора профиля на квазистационарный гидравлический расчёт  
TEST_F(IsothermalQuasistaticModel, ShowProfileImpactInQuasiStationaryModel)
{
    // Создаём папку с результатами для расчёта c профилем по первой и последней точкам трубопровода
    string path_start_end_profile = prepare_research_folder_for_qsm_model("start_end_profile");
    // Создаём папку с результатами для расчёта c полным профилем
    string path_full_profile = prepare_research_folder_for_qsm_model("path_full_profile");
    // Исходные данные для начального стационарного расчета
    constexpr double density_initial = 850;
    isothermal_quasistatic_task_boundaries_t initial_boundaries({ 0.2, 6e6, density_initial, 15e-6 });

    // Временные ряды краевых условий для квазистационарного расчета
    // Даем скачок по плотности на +10 кг/м^3
    vector<pair<string, double>> timeseries_initial_values = {
        { "Q", initial_boundaries.volumetric_flow }, // "Q" Расход по всей трубе (опционально), (м^3/с)
        { "p_in", initial_boundaries.pressure_in }, // "p_in" Давление на входе (опционально), (Па)
        { "rho_in", 10 + density_initial }, // "rho_in" Плотность жидкости, (кг/м3)
        { "visc_in", initial_boundaries.viscosity }, // "visc_in" Вязкость жидкости, (м2/сек)
    };
    // Объявляем структуру с исходными данными и настроечными параметрами
    timeseries_generator_settings settings = timeseries_generator_settings::default_settings();
    settings.value_relative_decrement = 0;
    settings.value_relative_increment = 0;
    settings.sample_time_max = 200;
    settings.sample_time_min = 200;

    vector<double> profile = create_honest_profile();

    pipe.profile.heights = profile;

    // Вызываем метод расчета квазистационарной модели с помощью МХ для полного профиля 
    vector_timeseries_t time_series = generate_timeseries(timeseries_initial_values, settings);
    perform_quasistatic_simulation<advection_moc_solver>(
        path_full_profile, initial_boundaries, time_series);

    pipe.profile = PipeProfile::create(
        pipe.profile.getPointCount(), 
        pipe.profile.coordinates.front(),
        pipe.profile.coordinates.back(),
        profile.front(),
        profile.back(),
        10e6
    );
    // Вызываем метод расчета квазистационарной модели с помощью МХ для профиля по первой и последней точкам
    vector_timeseries_t time_series_2 = generate_timeseries(timeseries_initial_values, settings);
    perform_quasistatic_simulation<advection_moc_solver>(
        path_start_end_profile, initial_boundaries, time_series_2);
}



