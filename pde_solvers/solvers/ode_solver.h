#pragma once


/// @brief ������� ��� ������� ������ �� ������ ���������-���������
/// @param ode ������� ���
/// @param direction ����������� �������: +1 �� ���� ��������, -1 ������ ���� ��������
/// @param initial_condition ��������� �������. 
/// ���� direction = +1, �� ��� ����� ��������� �������, ���� direction = -1, �� ������
/// @param _result ����� ��� ������ ����������
template <size_t Dimension, typename ResultBuffer>
inline void solve_euler_corrector(
    ode_t<Dimension>& ode,
    int direction,
    const typename ode_t<Dimension>::var_type& initial_condition,
    ResultBuffer* _result
)
{
    ResultBuffer& result = *_result;

    typedef typename fixed_system_types<Dimension>::var_type vector_type;
    const vector<double>& grid = ode.get_grid();

    if (result.size() != grid.size())
        throw std::runtime_error("Result buffer and grid size must be equal");

    // Direction == +1
    int start_index = direction > 0 ? 0 : static_cast<int>(grid.size()) - 1;
    int end_index = direction < 0 ? 0 : static_cast<int>(grid.size()) - 1;

    result[start_index] = initial_condition;

    //after_calc_event(start_index, result[start_index]);

    for (int index = start_index; index != end_index /*������� ������ ������������ � ��� ���������*/; index += direction) {
        int next_index = index + direction;

        vector_type u_prev = result[index];
        double dx = grid[next_index] - grid[index];

        // Predictor
        vector_type predictor_gradient = ode.ode_right_party(index, u_prev);
        vector_type prediction = u_prev + dx * predictor_gradient;

        // Corrector
        vector_type corrector_gradient = 0.5 * (predictor_gradient + ode.ode_right_party(next_index, prediction));

        result[next_index] = u_prev + dx * corrector_gradient;

        //if (has_not_finite(u_next)) {
        //    predictor_gradient = ode->right_party(x, u_prev);
        //    corrector_gradient = 0.5 * (predictor_gradient + ode->right_party(x, prediction));
        //    throw std::logic_error("solve_euler_corrector() not finite u_next");
        //}

        //after_calc_event(next_index, u_next);
    }
}



