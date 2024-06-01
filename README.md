# Инструменты pde_solvers
## Оглавление
* [Организация хранения расчетных профилей в буферах pde_solvers](#Организация-хранения-расчетных-профилей-в-буферах-pde_solvers)
  * [Идея буфера pde_solvers](#Идея-буфера-pde_solvers)
  * [Пример использования буфера pde_solvers](#Пример-использования-буфера-pde_solvers)
  * [Проблемно-ориентированный буфер](#Проблемно-ориентированный-буфер)
* [Реализация метода Эйлера для решения дифференциальных уравнений](#Реализация-метода-Эйлера-для-решения-дифференциальных-уравнений)
  * [Пример использования solve_euler ](#Пример-использования-solve_euler )
* [Организация работы с временными рядами в pde_solvers](#Организация-работы-с-временными-рядами-в-pde_solvers)
  * [Считывание исторических данных по тегу](#Считывание-исторических-данных-по-тегу)
  * [Работа с временными рядами](#Работа-с-временными-рядами)
  * [Пример работы с временными рядами](#Пример-работы-с-временными-рядами)
  * [Синтетические временные ряды](#Синтетические-временные-ряды)
  * [Пример работы с синтетическими временными рядами](#Пример-работы-с-синтетическими-временными-рядами)
* [Исследование квазистационарной гидравлической модели](#Исследование-квазистационарной-гидравлической-модели)
  * [Модель на численном методе Quickest Ultimate с постоянным временным шагом](#Модель-на-численном-методе-Quickest-Ultimate-с-постоянным-временным-шагом)
  * [Модель на методе характеристик с постоянным временным шагом](#Модель-на-методе-характеристик-с-постоянным-временным-шагом)
  * [Модель на методе характеристик с переменным временным шагом](#Модель-на-методе-характеристик-с-переменным-временным-шагом)
* [Создание профиля с постоянным шагом по координате из исходного профиля с реального трубопровода](#Создание-профиля-с-постоянным-шагом-по-координате-из-исходного-профиля-с-реального-трубопровода)
  * [Алгоритм создания нового профиля](#Алгоритм-создания-нового-профиля)
  * [Использование инструмента из pde_solvers](#Использование-инструмента-из-pde_solvers)
## Организация хранения расчетных профилей в буферах pde_solvers

Сущность `ring_buffer_t` предназначена для хранения расчетных профилей, содержащих
значения произвольного типа

### Идея буфера pde_solvers

При использовании метода характеристик для расчета профиля в текущий момент времени 
нужен профиль в предыдущий момент времени. На первый взгляд, кажется удачной идея 
организовать массив слоев, который будет дополняться все новыми  и новыми слоями 
в процессе расчета. Такой подход избыточен – в момент времени t<sub>k</sub>  для расчета слоя нужен лишь слой, соответствующий моменту t<sub>k-1</sub> 

Хорошей идеей выглядит создание массива размерности 2 для хранения текущего и предыдущего слоя. 
Но в процессе расчета текущий и предыдущий слои всё время меняются местами – как только 
рассчитан k-ый слой на основе (k-1)-ого, сам k-ый слой становится предыдущим для (k+1)-ого. 
Постоянно менять местами значения элементов массива не оптимально с точки зрения ресурсов. 
Решение – запоминать индекс текущего слоя, из предыдущих слоев хранить только необходимые для расчета.
Используется циклический сдвиг индекса текущего слоя. Положение самих слоев 
при этом не меняется (см. Рисунок).

![Схема, поясняющая идею циклического сдвига индекс текущего слоя в буфере][ring_buffer_schema]

[ring_buffer_schema]: images/ring_buffer_schema.png "Схема, поясняющая идею циклического сдвига индекс текущего слоя в буфере"  

<div align = "center">
</center><b>Схема, поясняющая идею циклического сдвига индекс текущего слоя в буфере</b></center>
</div>
<br>

Чтобы еще больше упросить себе жизнь, нужно запоминать индекс текущего слоя в самом буфере. 
Решение – реализовать логику смещения индекса внутри самого буфера. Вырисовывается реализация 
буфера как класса. Слои и текущий индекс будут данными класса. Методы должны позволять менять 
слой, который считается текущим; получать текущий слой и предыдущий.
Описанные идеи реализованы в буфере pde_solvers. Методы буфера pde_solvers освобождают от 
необходимости запоминать индекс текущего слоя вне самого буфера.

Метод ```.advance(i)```
циклически сдвигает индекс текущего слоя на заданную величину *i*.  

Метод ```.current()```
возвращает ссылку на текущий слой буфера.  

Метод ```.previous()```
возвращает ссылку на предыдущий слой буфера.

### Пример использования буфера pde_solvers

Предположим, что перед нами стоит задача расчета движения партий. Будем моделировать 
изменение плотности жидкости в трубе при помощи метода характеристик. 

**Решение задачи многослойного расчета плотности методом характеристик с сохранением текущего и предыдущего слоя в буфере выглядит следующим образом:**

```C++
// Длина трубы, м
double L = 3000;
// Количество точек расчетной сетки
int grid_size = 100;
// Граничное условие для плотности
double left_condition_density = 860;
// Начальное значение плотности
double initial_condition_density = 850;
// Cкорость потока, м/с
double v = 1.5;
// Величина шага между узлами расчетной сетки, м
double dx = L / (grid_size - 1);
// Шаг во времени из условия Куранта
double dt = dx / v;

// Время моделирования, с
// Вместе со значением числа Куранта определяет, 
// какое количество слоев будет рассчитано
double T = 10e3; // время моделирования участка трубопровода, с 

// Создаем слой, заполненный начальным значением плотности
// Далее используем его для инициализации слоев буфера
vector<double> initial_layer(grid_size, initial_condition_density);

// Создание буфера для решаемой задачи
// 2 - количество слоев в буфере
// initial_layer - слой, значениями из которого проинициализируются все слои буфера
ring_buffer_t<vector<double>> buffer(2, initial_layer);

for (double time = 0; time < T; time += dt)
{
	// Получение ссылок на текущий и предыдущий слои буфера
	vector<double>& previous_layer = buffer.previous();
	vector<double>& current_layer = buffer.current();

	// Расчет методом характеристик
	// Суть - смещение предыдущего слоя и запись граничного условия
	for (size_t i = 1; i < current_layer.size(); i++)
	{
		current_layer[i] = previous_layer[i - 1];
	}
	current_layer[0] = left_condition_density;
	
	// Слой current_layer на следующем шаге должен стать предыдущим. 
	// Для этого сместим индекс текущего слоя в буфере на единицу
	buffer.advance(+1);
}
```

------------

### Проблемно-ориентированный буфер

При выполнении сложных вычислений как правило вычисляется сразу несколько расчетных, например, одновременно производится расчет плотности и вязкости нефти в трубопроводе. В этом случае о слое можно думать, как о массиве массивов vector<vector<double>>. Однако, при обращении к профилям плотности и вязкости надо все время придерживаться порядка индексов. Пример:

```C++
	vector<double> initial_density(grid_size, 850); // плотность 850 кг/м3
	vector<double> initial_viscosity(grid_size, 13e-6); // вязкость 13 сСт
	vector<vector<double>> initial_layer{ initial_density, initial_viscosity };
	ring_buffer_t<vector<double>> buffer(2, initial_layer);
	buffer.current()[0] // здесь профиль плотности
	buffer.current()[1] // здесь профиль вязкости
```

С ростом количества рассчитываемых параметров в расчетной задаче данная проблема усугубляется. Представьте себе, что расчетных параметров 10 шт – запутаться в индексах будет очень просто. Поэтому при работе с буферами целесообразно использовать концепцию **проблемно-ориентированного слоя**. Подчеркнем, что речь идет о концепции, т.е. типовом подходе к реализации задач, а не об использовании некоторого имеющегося кода.
Пусть расчетная задача состоит в моделировании движения партий нефти по трубе, причем свойства партий описываются плотностью и вязкостью. Наиболее естественным представлением слоя данных является структура, в которой имеются профили плотности и вязкости.

```C++
struct density_viscosity_layer
{
    vector<double> density;
    vector<double> viscosity;
};
```

Чтобы завести буфер таких слоев, используем шаблонный параметр в `ring_buffer_t`

```C++
density_viscosity_layer initial_layer;
initial_layer.density = vector<double>(grid_size, 850);
initial_layer.viscosity = vector<double>(grid_size, 13e-6);
ring_buffer_t<density_viscosity_layer> buffer(2, initial_layer);
buffer.current().density // здесь профиль плотности
buffer.current().viscosity // здесь профиль вязкости
```


## Реализация метода Эйлера для решения дифференциальных уравнений

`solve_euler` это функция, которая решает математическую задачу – находит решение заданной системы 
обыкновенных дифференциальных уравнений (ОДУ) методом Эйлера.

В `pde_solvers` система ОДУ представлена базовым классом `ode_t`. Система уравнений 
в частных производных представлена базовым классом `pde_t`. 

На уровне идеи эти сущности представляют собой матричную запись системы дифференциальных уравнений. 
Эти сущности предоставляют методы, позволяющие численно найти решение ДУ.

Численное решение ОДУ методом Эйлера предполагает:
* расчет производной f'(x<sub>i</sub> ) в i-ом узле расчетной сетки
* оценку приращения функции по значению производной ∆y = f'(x<sub>i</sub>)∙(x<sub>i+1</sub>-x<sub>i</sub>)
* расчет значения функции в следующем узле сетки на основании оценки приращения функции: y<sub>i+1</sub> = y<sub>i</sub>+∆y

Соответственно, методы `ode_t` должны позволять рассчитывать значение производной 
в данной точке расчетной сетки или при данных значениях аргументов.

Методы базовых классов ode_t имеют одинаковый набор параметров:
`имя_метода(grid_index, point_vector)`
* где `grid_index` - индекс текущего узла расчетной сетки. Его можно использовать 
для дифференцирования параметра, профиль которого на данной расчетной сетке известен 
(например, гидростатический перепад dz/dt при известном профиле трубы);
* `point_vector` – вектор значений неизвестных в данной точке.

`solve_euler` работает с сущностями типа ode_t и ничего не знает о физическом смысле 
решаемой задачи. Сведение определенной задачи трубопроводного транспорта 
к базовому классу ode_t или pde_t – ответственность разработчика, использующего `pde_solvers`. 

Это сведение выполняется созданием класса, производного от базовых классов `ode_t` или `pde_t`. 
Производные классы переопределяют поведение функций базового класса  на основе математической модели задачи.


### Пример использования solve_euler

Используем solve_euler для расчет профиля давления в классической задаче PQ\
(см. [Лурье 2012](https://elib.gubkin.ru/content/19749 "Пособие в электронной нефтегазовой библиотеке"), раздел 4.2, Задача 1)

**Решаемое диффеенциальное уравнение реализовано следующим образом:**
```C++
/// @brief Уравнение трубы для задачи PQ
class Pipe_model_for_PQ_t : public ode_t<1>
{
public:
    using ode_t<1>::equation_coeffs_type;
    using ode_t<1>::right_party_type;
    using ode_t<1>::var_type;
protected:
    pipe_properties_t& pipe;
    oil_parameters_t& oil;
    double flow;

public:
    /// @brief Констуктор уравнения трубы
    /// @param pipe Ссылка на сущность трубы
    /// @param oil Ссылка на сущность нефти
    /// @param flow Объемный расход
    Pipe_model_for_PQ_t(pipe_properties_t& pipe, oil_parameters_t& oil, double flow)
        : pipe(pipe)
        , oil(oil)
        , flow(flow)
    {

    }

    /// @brief Возвращает известную уравнению сетку
    virtual const vector<double>& get_grid() const override {
        return pipe.profile.coordinates;
    }

    /// @brief Возвращает значение правой части ДУ
    /// @param grid_index Обсчитываемый индекс расчетной сетки
    /// @param point_vector Начальные условия
    /// @return Значение правой части ДУ в точке point_vector
    virtual right_party_type ode_right_party(
        size_t grid_index, const var_type& point_vector) const override
    {
        double rho = oil.density();
        double S_0 = pipe.wall.getArea();
        double v = flow / (S_0);
        double Re = v * pipe.wall.diameter / oil.viscosity();
        double lambda = pipe.resistance_function(Re, pipe.wall.relativeRoughness());
        double tau_w = lambda / 8 * rho * v * abs(v);
        /// Обработка индекса в случае расчетов на границах трубы
        /// Чтобы не выйти за массив высот, будем считать dz/dx в соседней точке
        grid_index = grid_index == 0 ? grid_index + 1 : grid_index;
        grid_index = grid_index == pipe.profile.heights.size() - 1 ? grid_index - 1 : grid_index;

        double height_derivative = (pipe.profile.heights[grid_index] - pipe.profile.heights[grid_index - 1]) /
            (pipe.profile.coordinates[grid_index] - pipe.profile.coordinates[grid_index - 1]);

        return { ((-4) / pipe.wall.diameter) * tau_w - rho * M_G * height_derivative };
    }

};
```
**Применение solve_euler к разработаному классу `Pipe_model_for_PQ_t`**

```C++
int main()
{
    // Создаем сущность трубы
    pipe_properties_t pipe;
    
    // Создаем сущность нефти
    oil_parameters_t oil;

    // Задаем объемнй расход нефти, [м3/с]
    double Q = 0.8;

    // Создаем расчетную модель трубы
    Pipe_model_for_PQ_t pipeModel(pipe, oil, Q);

    // Получаем указатель на начало слоя в буфере
    profile_wrapper<double, 1> start_layer(buffer.current());

    // Задаем конечное давление
    double Pout = 5e5;

    // Вектор для хранения профиля давления
    vector<double> profile(0, pipe.profile.getPointCount());

    /// Модифицированный метод Эйлера для модели pipeModel,
    /// расчет ведется справа-налево относительно сетки,
    /// начальное условие Pout, 
    /// результаты расчета запишутся в слой, на который указывает start_layer
    solve_euler_corrector<1>(pipeModel, -1,  Pout , &profile);
}
```
## Организация работы с временными рядами в pde_solvers

### Считывание исторических данных по тегу

Для имитации работы трубопровода необходимо задаваться краевыми условиями на каждом шаге моделирования, в качестве которых чаще всего выступают исторические данные – временные ряды СДКУ. Информация о параметре хранится в файле формата `.csv`, в названии которого тег параметра. При этом физическая величина может хранится в единицах измерения отличных от СИ. Также, такой файл может включать в себя данные за достаточно большой период.

В `pde_solvers`  существуют инструменты `csv_tag_reader` и `csv_multiple_tag_reader`, предназначенные для автоматического чтения временных рядов одного или сразу нескольких параметров соответственно с возможностью указания интересующего периода, а также функцией автоматического перевода в СИ. В результате чтения каждого файла получаем временной ряд – пара векторов, в которой первый вектор представляет собой моменты времени, а второй – значения параметра.

Посмотреть, какие варианты перевода единиц измерения реализованы, можно в файле `timeseries_helpers.h`. Поле `units` класса `dimension_converter`  содержит список всех реализованных вариантов перевода единиц измерения в СИ.

### Работа с временными рядами

В ходе моделирования шаг по времени в общем случае не совпадает с шагом временных рядов, отсюда возникает необходимость интерполяции исторических данных. Надо синхронизировать временные ряды с шагом численного метода, который в общем случае может быть переменным.

![Временные ряды краевых условий и временная сетка численного метода][timeseries_schema]

[timeseries_schema]: images/timeseries_schema.png "Временные ряды краевых условий и временная сетка численного метода"  

<div align = "center">
</center><b>Временные ряды краевых условий и временная сетка численного метода</b></center>
</div>
<br>

Для этого в `pde_solvers` существует класс `vector_timeseries_t`, принимающий вектор считанных по тегам временных рядов в описанном выше формате и имеющий функцию подготовки среза значений параметров на заданный момент времени.

### Пример работы с временными рядами

Пример работы с временными рядами приведён в файле `test_timeseries.h` в тесте `Timeseries.UseCase`

### Синтетические временные ряды

При отсутствии временных рядов имеет место создание синтетических временных рядов, которые будут выглядеть аналогично временным рядам СДКУ. Синтетические временные ряды, как и реальные ряды СДКУ, должны иметь следующие схожие черты:
* Непостоянный шаг во времени;
* Наличие шума (ошибка измерений датчиков);
* Возможность принимать синтезированные данные в компоненте vector_timeseries_t;
* Возможность выбора начала и конца момента времени при создании синтетических временных рядов;

**Скачок по значениям данных в синтетических временных рядах**

В работе с синтетическими временными рядами присутствует функционал скачка по значениям данных. Это означает, что в определённый момент времени значение выбранного параметра может резко измениться на заданную величину.
Для реализации этой функции была добавлена функция `apply_jump`. Она позволяет увеличивать или уменьшать значение выбранного параметра на определённую величину, начиная с заданного момента времени. Это может быть полезно для моделирования различных ситуаций, например, для анализа изменений в расходах или других параметрах.
Добавление функции `apply_jump` было сделано с целью реализации многопартийности. Это означает, что можно моделировать различные сценарии развития событий, меняя значения параметров в определённые моменты времени.

![Временной ряд расхода с примененным скачком](https://github.com/victorsouth/pde_solvers/assets/128503756/ecd1354d-8566-44e8-b8df-7b3e4fa494ff)

Данный график демонстрирует поведение генератора временных рядов с имитацией показаний датчиков и последующее внедрение скачка по расходу в этот временной ряд.
На графике красной штрих-линией обозначены границы разброса значений данных во временном ряду. Чёрной линией обозначено изначальное значение расхода, которое мы указываем при создании временного ряда. Синей линией показан полученный в итоге временной ряд расхода.
В момент времени t_0 происходит скачок, и к значениям расхода с выбранного момента времени добавляются значения ΔQ. Это позволяет исследовать динамику изменения расхода и выявить возможные отклонения от изначальных значений.

### Пример работы с синтетическими временными рядами

Пример работы с временными рядами приведён в файле `test_synthetic_timeseries.h` в тесте `SyntheticTimeSeries.UseCase_PrepareTimeSeries`

## Исследование квазистационарной гидравлической модели

Исследования квазистационарной гидравлической изотермической модели проводились в файле quick_with_quasistationary_model.h. Этот файл включает в себя следующие тесты:
*Модель на численном методе Quickest Ultimate с постоянным временным шагом (Cr ≈ 0.8-0.9)
  *Исследование возникновения ступенчатой партии при использовании синтетических временных рядов со случайной генерацией (QuasiStationaryModel, IdealQuickWithQuasiStationaryModel)
*Модель на методе характеристик с постоянным временным шагом (Cr ≈ 0.8-0.9)
  *Исследование возникновения ступенчатой партии при использовании синтетических временных рядов со случайной генерацией (QuasiStationaryModel, MocWithQuasiStationaryModel)
*Модель на методе характеристик с переменным временным шагом (Cr=1)
  *Исследование возникновения ступенчатой партии при использовании синтетических временных рядов со случайной генерацией (QuasiStationaryModel, OptionalStepMocWithQuasiStationaryModel)
  *Исследование возникновения ступенчатой партии при идеальных условиях (QuasiStationaryModel, IdealMocWithQuasiStationaryModel)
  *Исследование возникновения импульсной партии при идеальных условиях (QuasiStationaryModel, IdealImpulsMocWithQuasiStationaryModel)
Во всех этих исследованиях рассматривается конец трубы в момент смесеобразования. Графики профилей плотности и разницы давлений получены в один момент времени.

### Модель на численном методе Quickest Ultimate с постоянным временным шагом

![Квазистационарная модель на методе Quickest Ultimate](https://github.com/victorsouth/pde_solvers/assets/128503756/cc4a8c4b-16f9-49f6-8a17-7ace0372c161)

Здесь растяжение численной диффузии допустимое (это было выяснено после сравнения численной диффузии этого численного метода с реальной физической диффузией). Тут численная диффузия играет роль турбулентной, происходит имитация смесеобразования.

### Модель на методе характеристик с постоянным временным шагом

Метод характеристик, как известно, обладает сильной численной диффузией, в следствии этого возникает сильная ошибка при расчете профиля давления. Отчетливо эту ошибку можно увидеть на графике дифференциального профиля.

![Квазистационарная модель на методе характеристик](https://github.com/victorsouth/pde_solvers/assets/128503756/e498780d-0568-4e7a-8f9f-c0925ce3291b)

Здесь видно, как сильно растягивается скачек по плотности, в следствии этого полученный дифференциальный профиль так же имеет сильное растяжение в моменте замещения партий.

### Модель на методе характеристик с переменным временным шагом

![Квазистационарная модель на методе характеристик с переменным шагом](https://github.com/victorsouth/pde_solvers/assets/128503756/717bc9e7-691f-4265-9c76-a822efc19b88)

Как видно из графика, численная диффузия минимальна, так как каждый раз шаг метода характеристик пересчитывался из-за новой скорости на входе временного ряда. Из-за присутствия переменного шага количество данных при моделировании уменьшается. Так же возникают трудности при внедрении этого численного метода в математическую модель тренажера, нам нужен постоянный временной шаг. Помимо этого, из-за отсутсвия численной диффузии происходит поршневое вытеснение, что говорит об отсутсвии смесеобразования в точке соприкосновения партий.

**Исследование возникновения ступенчатой партии при идеальных условиях**

![Исследование скачка по плотности при постоянном расходе (новая партия еще не зашла в трубопровод)](https://github.com/victorsouth/pde_solvers/assets/128503756/c190a0b2-7ebf-4e13-a28a-8ec131184617)

![Исследование скачка по плотности при постоянном расходе (новая партия начинает заходит в трубопровод)](https://github.com/victorsouth/pde_solvers/assets/128503756/fc9731cf-6187-424b-9caa-5648a97c90a9)

![Исследование скачка по плотности при постоянном расходе (новая партия наполовину вытеснила старую)](https://github.com/victorsouth/pde_solvers/assets/128503756/68173a81-9533-4437-a5a5-fc60e4f2a1bd)

По трем приведённым сверху рисункам видно, как скачок по плотности влияет на профиль. При поддержании постоянным давления на входе, давление по всей трубе резко начинает увеличиваться. при этом, чем больше новая партия заходит в трубопровод, тем выше поднимается давление по всей трубе. Со временем новая партия полностью заместит старую, и будет следующий результат:

![Исследование скачка по плотности при постоянном расходе (новая партия полностью вытеснила старую)](https://github.com/victorsouth/pde_solvers/assets/128503756/794185e5-ee85-4a99-be54-47c14ed28300)

Из графиков профилей давлений, где красной линией изображен профиль после, а синей линией – до, видно, что новый профиль трубы находится выше, но не целиком, на входе трубы они находятся на одной точке. Увеличение давления обуславливается тем, что нефть стало легче перекачивать, так как плотность новой партии у нефти ниже.

**Исследование возникновения импульсной партии при идеальных условиях**

![Исследование импульса по плотности при постоянном расходе (новая партия еще не зашла в трубопровод)](https://github.com/victorsouth/pde_solvers/assets/128503756/6ad0d07d-b47c-4dd9-bccd-c26a1fad4045)

![Исследование импульса по плотности при постоянном расходе (новая партия начинает заходит в трубопровод)](https://github.com/victorsouth/pde_solvers/assets/128503756/f6e84bfe-92e3-480c-93d4-e8ef13f82fb1)

![Исследование импульса по плотности при постоянном расходе (старая партия возвращается)](https://github.com/victorsouth/pde_solvers/assets/128503756/d03cd175-7f26-471b-a2f5-e2e7e7f9e2e9)

Как видно из графиков, на первых двух поведение при заходе новой партии идентичное, но с возвращением старой партии, видно, как давление на выходе перестает расти, это объясняется тем, что, независимо от положения партии (рисунок 4.7) суммарный объем, или масса нефти в трубе не меняется, соответственно не меняется и трудность перекачки нефти.

![Исследование импульса по плотности при постоянном расходе (импульс новой партии плотности достиг середины трубопровода)](https://github.com/victorsouth/pde_solvers/assets/128503756/16e18cf1-9980-45e5-8774-6c91f28dd3fd)

Но изменения у профиля все же присутствуют. С движением партии с меньшей плотностью, вместе с ней меняется профиль давлений. Хоть и давление на выходе повышенное, по всей трубе все тяжелее становится перекачивать нефть со старой большей по плотности партией. Когда импульс уходит из трубопровода полностью, профиль возвращается в прошлое состояние.

![Исследование импульса по плотности при постоянном расходе (импульс плотности полностью вышел из трубопровода)](https://github.com/victorsouth/pde_solvers/assets/128503756/91962c3d-1a5b-47b4-bdd7-27127c0f572d)

Можно сделать вывод, что есть смысл учитывать квазистационар. Если бы партийность нефти не учитывалась, то при определении давления в середине трубы в момент переходного процесса вытеснения старой партии нефти новой присутствовала значительная ошибка.

## Создание профиля с постоянным шагом по координате из исходного профиля с реального трубопровода

Математическая модель умеет работать только с профилем, имеющим сетку с постоянным шагом по координате. Но в реальности информация о профиле трубопровода представлена с разным расстоянием между точками. 
В pde_solvers существует инструмент для преобразования исходного профиля в профиль, необходимый для моделирования работы трубопровода.

### Алгоритм создания нового профиля 
Перед тем как использовать этот инструмент необходимо задаться желаемой длинной сегмента для новой сетки. В результате получится профиль с постоянным шагом равным или близким к желаемому.

1. Сначала рассчитывается количество сегментов, при этом происходит округление в меньшую сторону, ввиду чего длина сегмента естественным образом увеличивается по сравнению с желаемой
$$n_{сег} \approx  \frac{L_{трубы}} {\Delta x_{жел}}$$
$$\Delta x_{действ} =  \frac{L_{трубы}} {n_{сег}}$$
В случае короткой трубы, меньшей желаемой длины сегмента, новый профиль будет состоять из одного сегмента заданной длины 

2. Задаётся вектор координат нового профиля с полученным постоянным шагом. 

![Сравнение сеток координат исходного и итогового профилей][uniform_profile_steps]

[uniform_profile_steps]: images/uniform_profile_steps.png "Сравнение сеток координат исходного и итогового профилей"  

<div align = "center">
</center><b>Сравнение сеток координат исходного и итогового профилей</b></center>
</div>
<br>

3. Обрабатывается случай короткой трубы – дополняется исходный профиль точкой в конце:
![Обработка случая короткого исходного профиля][uniform_profile_short_profile]

[uniform_profile_short_profile]: images/uniform_profile_short_profile.png "Обработка случая короткого исходного профиля"  

<div align = "center">
</center><b>Обработка случая короткого исходного профиля</b></center>
</div>
<br>

4. Следующим этапом в случае разреженного исходного профиля пространство между координатами заполняется точками с шагом:
$$n_{промеж} \approx  \frac{\Delta x_{исх.трубы}} {\Delta x_{действ}}$$
$$\Delta x_{промеж} =  \frac{\Delta x_{исх.трубы}} {n_{промеж}}$$

![Увеличение плотности исходного профиля][uniform_profile_rare]

[uniform_profile_rare]: images/uniform_profile_rare.png "Увеличение плотности исходного профиля"  

<div align = "center">
</center><b>Увеличение плотности исходного профиля</b></center>
</div>
<br>

Достаточно плотные участки профиля остаются без изменений.

5. Чтобы определить высотные отметки соответствующих координат нового профиля применяется следующий метод:
    - Для каждой точки определяется область притяжения - по половине сегмента в каждую сторону
    - На области притяжения определяется максимальная высотная отметка и присваивается соответствующей координате
    - Так как начальной и конечной точкам нового профиля будут соответствовать высотки начальной и конечной точек исходного профиля, область притяжения второй и последней точек полученного профиля будет составлять полтора сегмента


![Области притяжения точек нового профиля][uniform_profile_area_attr]

[uniform_profile_area_attr]: images/uniform_profile_area_attr.png "Области притяжения точек нового профиля"  

<div align = "center">
</center><b>Области притяжения точек нового профиля</b></center>
</div>
<br>

  

![Область притяжения в начале профиля][uniform_profile_bound]

[uniform_profile_bound]: images/uniform_profile_bound.png "Область притяжения в начале профиля"  

<div align = "center">
</center><b>Область притяжения в начале профиля</b></center>
</div>
<br>

6.	Получаем профиль с постоянным шагом по координате

### Использование инструмента из pde_solvers

В `pde_solvers` реализовано два способа создания профиля с постоянным шагом по координате на основе исходного:

1. Функция `create_uniform_profile` принимает в качестве аргументов исходный профиль (класс `PipeProfile`) и желаемый шаг нового профиля
1. Если исходный профиль хранится в файле `.csv`, тогда можно использовать статичный метод `pipe_profile_uniform::get_uniform_profile_from_csv`, аргументами которого является путь к файлу и желаемый шаг нового профиля.
Исходный профиль должен хранится в следующем формате:



|km|heigths|
| -----|------|
|0|150|
|200|180|
|...|...|

Данные функции возвращают профиль с постоянным шагом по координате
